#include "app_task.h"

#include "app/matter_init.h"
#include "app/matter_event_handler.h"
#include "app/task_executor.h"
#include "clusters/identify.h"
#include "lib/core/CHIPError.h"

#include "app.hpp"
#include "app_config.hpp"
#include "net.hpp"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/power-source-server/power-source-server.h>
#include <app/clusters/soil-measurement-server/soil-measurement-cluster.h>
#include <app/server/Server.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app; /* NB: this makes a bare `app::` ambiguous with HappyPot's own
				namespace -- reach for the latter as ::app:: */
using namespace ::chip::DeviceLayer;
using namespace ::chip::app::Clusters;

namespace
{
/* Endpoint map -- see src/default_zap/happypot.zap. One probe, one endpoint: the Soil Sensor
 * device type (0x0045, Matter 1.5) carries Identify + Descriptor + Soil Measurement (0x0430). */
constexpr EndpointId kPowerSourceEndpointId = 0; /* root node */
constexpr EndpointId kSensorEndpointId = 1;

/* The battery powers the whole node. */
EndpointId sPoweredEndpoints[] = { 0, 1 };

Nrf::Matter::IdentifyCluster sIdentifyCluster(kSensorEndpointId);

/* The Soil Measurement cluster is served by this code-driven DefaultServerCluster (the new
 * registry style -- there is no ember-backed storage for it, which is why the ZAP marks its
 * attributes "External"). Created and registered in the cluster's ember init callback below,
 * which fires while StartServer() configures the endpoints. */
LazyRegisteredServerCluster<SoilMeasurementCluster> sSoilCluster;

/* What the probe can OUTPUT: the full percent scale, with the probe's honest accuracy. */
const Globals::Structs::MeasurementAccuracyRangeStruct::Type kSoilAccuracyRanges[] = {
	{ .rangeMin = 0, .rangeMax = 100, .percentMax = MakeOptional(static_cast<chip::Percent100ths>(1000)) }
};

const SoilMeasurement::Attributes::SoilMoistureMeasurementLimits::TypeInfo::Type kSoilMeasurementLimits = {
	.measurementType = Globals::MeasurementTypeEnum::kSoilMoisture,
	.measured = true,
	.minMeasuredValue = 0,
	.maxMeasuredValue = 100,
	.accuracyRanges =
		DataModel::List<const Globals::Structs::MeasurementAccuracyRangeStruct::Type>(kSoilAccuracyRanges),
};

/* HappyPot's own thread. It renders a 200x200 mono frame with 40 px fonts and blocks ~3 s in an
 * e-paper refresh, so it gets the big stack -- and it gets it here rather than on the CHIP event
 * loop, which OpenThread needs responsive. Priority below the cooperative CHIP/OT threads. */
K_THREAD_STACK_DEFINE(sHappyPotStack, 8192);
k_thread sHappyPotThread;

/* The hook runs on HappyPot's thread, so it takes the CHIP stack lock -- the documented way to
 * touch cluster attributes from outside the event loop. Holding it across the setters keeps the
 * reading self-consistent for anyone reading it at that moment; none of them block. */
void PublishReading(const ::net::Reading &r)
{
	PlatformMgr().LockChipStack();

	/* Checked, because it can refuse (not yet registered, value out of range) -- and a soil
	 * number that quietly stops moving is worse than one that is missing. */
	if (sSoilCluster.IsConstructed()) {
		const CHIP_ERROR err = sSoilCluster.Cluster().SetSoilMoistureMeasuredValue(
			DataModel::MakeNullable(static_cast<chip::Percent>(r.soil.percent)));
		if (err != CHIP_NO_ERROR) {
			LOG_ERR("Soil %d %% was not published (%s)", r.soil.percent, err.AsString());
		}
	}

	/* The cell terminal voltage, in mV (batVoltage is int32u mV). Matter counts the battery
	 * in half percent. */
	PowerSource::Attributes::BatVoltage::Set(kPowerSourceEndpointId, r.battery.mv);
	PowerSource::Attributes::BatPercentRemaining::Set(kPowerSourceEndpointId,
							  static_cast<uint8_t>(r.battery.percent * 2));
	PowerSource::Attributes::BatChargeLevel::Set(
		kPowerSourceEndpointId, (r.battery.percent <= cfg::LOW_BATTERY_PCT)
						? PowerSource::BatChargeLevelEnum::kCritical
						: PowerSource::BatChargeLevelEnum::kOk);

	PlatformMgr().UnlockChipStack();
}

/* Runs on the CHIP thread; only records the state, the loop reads it on its next pass. */
void MatterEventHandler(const ChipDeviceEvent *event, intptr_t)
{
	if (event->Type == DeviceEventType::kThreadConnectivityChange) {
		::net::set_link_up(event->ThreadConnectivityChange.Result == kConnectivity_Established);
	}
}

/* Fabric membership, event-driven: committed and removed are the only two edges, and the fabric
 * table calls both on the CHIP thread. */
class FabricDelegate : public chip::FabricTable::Delegate {
	void OnFabricCommitted(const chip::FabricTable &table, FabricIndex) override
	{
		::net::set_commissioned(table.FabricCount() > 0);
	}
	void OnFabricRemoved(const chip::FabricTable &table, FabricIndex) override
	{
		::net::set_commissioned(table.FabricCount() > 0);
	}
};
FabricDelegate sFabricDelegate;

/* The onboarding codes, fetched once from CHIP. Static because net::set_radio() does not copy
 * them and the UI holds the pointers for the life of the device. */
char sQrCode[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
char sManualCode[chip::kManualSetupLongCodeCharLength + 1];
char sManualCodePretty[sizeof(sManualCode) + 2]; /* room for the two dashes */

/* Group the manual code 4-3-4 as every controller UI does -- eleven bare digits are read wrong.
 * Only the 11-digit short code; anything unexpected passes through unmangled. */
const char *GroupManualCode(const char *digits)
{
	if (strlen(digits) != 11) {
		return digits;
	}
	snprintf(sManualCodePretty, sizeof(sManualCodePretty), "%.4s-%.3s-%.4s", digits, digits + 4,
		 digits + 7);
	return sManualCodePretty;
}

/* What a controller needs to find and authenticate this device -- the same payload the sample
 * prints to the log at boot, but printed on the e-paper, where the user actually is. It does not
 * change over the device's life, so read it once and hand it to the UI. */
void FetchOnboardingCodes()
{
	chip::MutableCharSpan qr(sQrCode);
	chip::MutableCharSpan manual(sManualCode);
	const chip::RendezvousInformationFlags rendezvous(chip::RendezvousInformationFlag::kBLE);

	if (GetQRCode(qr, rendezvous) != CHIP_NO_ERROR ||
	    GetManualPairingCode(manual, rendezvous) != CHIP_NO_ERROR) {
		LOG_ERR("Could not read the onboarding codes; no pairing screen");
		::net::set_radio({ "Matter over Thread", nullptr, nullptr, nullptr });
		return;
	}

	::net::set_radio({ "Matter over Thread", sQrCode, GroupManualCode(sManualCode), nullptr });
}

/* How long the radio listens after the user asks for the code: enough to fetch a phone, not the
 * boot window's full hour (CONFIG_CHIP_BLE_ADVERTISING_DURATION) -- that is right for a
 * factory-new device, too generous for a gesture. */
constexpr auto kPanelPairingWindow = System::Clock::Seconds32(10 * 60);

/* The code is on the panel: start listening, or the QR is a dead letter once the boot hour is
 * over. The two guards mirror Nordic's Board::StartBLEAdvertisement: a commissioned device must
 * not re-open (a second commissioner could join uninvited), and an already-advertising one must
 * not be restarted -- that would drop a PASE session seconds from completing. The second guard
 * also means a device inside its boot hour keeps the hour. Runs on HappyPot's thread; takes the
 * stack lock. */
void OpenPairingWindow()
{
	PlatformMgr().LockChipStack();

	if (Server::GetInstance().GetFabricTable().FabricCount() != 0) {
		LOG_INF("Already commissioned; not re-opening the window");
	} else if (ConnectivityMgr().IsBLEAdvertisingEnabled()) {
		LOG_INF("Commissioning window already open");
	} else if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
			   kPanelPairingWindow) != CHIP_NO_ERROR) {
		LOG_ERR("Could not open the commissioning window");
	} else {
		LOG_INF("Commissioning window opened from the panel for %u s",
			kPanelPairingWindow.count());
	}

	PlatformMgr().UnlockChipStack();
}

/* The user held the button on a confirmation screen that said what this does. CHIP wipes the
 * fabrics and reboots; it schedules the work rather than doing it here, because we are on
 * HappyPot's thread and the storage belongs to the CHIP one. */
void FactoryReset()
{
	LOG_INF("Factory reset requested from the panel");
	Server::GetInstance().ScheduleFactoryReset();
}

void HappyPotThread(void *, void *, void *)
{
	const ::net::Hooks hooks = {
		.reading = PublishReading,
		.factory_reset = FactoryReset,
		.pairing_open = OpenPairingWindow,
	};
	::net::set_hooks(hooks);
	::app::run("Matter over Thread"); /* never returns */
}

} /* namespace */

/* Fires from Server::Init() -> emberAfEndpointConfigure() while StartServer() runs -- the moment
 * the registry exists for this endpoint. Overrides the ZAP-generated weak stub. */
void emberAfSoilMeasurementClusterInitCallback(EndpointId endpoint)
{
	if (endpoint != kSensorEndpointId) {
		LOG_ERR("Soil Measurement on unexpected endpoint %u", endpoint);
		return;
	}

	sSoilCluster.Create(endpoint, kSoilMeasurementLimits);

	const CHIP_ERROR err =
		CodegenDataModelProvider::Instance().Registry().Register(sSoilCluster.Registration());
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Soil Measurement cluster registration failed (%s)", err.AsString());
		return;
	}

	/* Null until the first real sample: "no measurement yet" is the honest boot state. */
	sSoilCluster.Cluster().SetSoilMoistureMeasuredValue(
		DataModel::Nullable<chip::Percent>());
}

CHIP_ERROR AppTask::Init()
{
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(MatterEventHandler, 0));
	ReturnErrorOnFailure(sIdentifyCluster.Init());
	ReturnErrorOnFailure(Nrf::Matter::StartServer());

	/* Only now: the ember endpoint tables exist once StartServer() has returned (the soil
	 * cluster registered itself from the init callback above, mid-StartServer). The event
	 * loop is live by now, so take the stack lock. */
	PlatformMgr().LockChipStack();
	PowerSourceServer::Instance().SetEndpointList(kPowerSourceEndpointId,
						      Span<EndpointId>(sPoweredEndpoints));
	PlatformMgr().UnlockChipStack();

	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	/* Before the thread starts: app::run() reads the codes when it builds the UI, and whether
	 * there are any is what decides that the menu has a Network row at all. The fabric table
	 * has been loaded from NVS by now, so the seed below is the state a reboot comes back to;
	 * the delegate keeps it current from there. */
	FetchOnboardingCodes();
	Server::GetInstance().GetFabricTable().AddFabricDelegate(&sFabricDelegate);
	::net::set_commissioned(Server::GetInstance().GetFabricTable().FabricCount() > 0);

	k_thread_create(&sHappyPotThread, sHappyPotStack, K_THREAD_STACK_SIZEOF(sHappyPotStack),
			HappyPotThread, nullptr, nullptr, nullptr, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&sHappyPotThread, "happypot");

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
