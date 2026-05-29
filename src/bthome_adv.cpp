#include "bthome_adv.h"

#include <string.h>
#include <bthome.h> // bthome-cpp library (lib/bthome-cpp/src)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"

LOG_MODULE_REGISTER(bthome, LOG_LEVEL_INF);

namespace
{

	bt_le_ext_adv *adv = nullptr;
	uint8_t packet_counter = 0;

	const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

	/** Advertising interval in 0.625 ms units. Only matters for the spacing
	 * within a multi-event burst; with num_events=1 a single event goes out. */
	constexpr uint16_t ADV_INTERVAL = 160;

	constexpr uint32_t FW_VERSION =
		(static_cast<uint32_t>(cfg::FW_VERSION_MAJOR) << 16) |
		(static_cast<uint32_t>(cfg::FW_VERSION_MINOR) << 8) |
		cfg::FW_VERSION_PATCH;

	/** Set the payload and emit exactly one advertising event (1 TX per primary
	 * channel), then the controller auto-stops. ext-adv set_data is non-blocking
	 * so the SoC sleeps right after the event. */
	int advertise_once(const bt_data *ad, size_t n)
	{
		int err = bt_le_ext_adv_set_data(adv, ad, n, nullptr, 0);
		if (err)
		{
			LOG_WRN("ext_adv_set_data failed (%d)", err);
			return err;
		}
		const bt_le_ext_adv_start_param start_param = {
			.timeout = 0,
			.num_events = 1,
		};
		err = bt_le_ext_adv_start(adv, &start_param);
		if (err)
		{
			LOG_WRN("ext_adv_start failed (%d)", err);
		}
		return err;
	}

} // namespace

int bthome_init(void)
{
	int err = bt_enable(nullptr);
	if (err)
	{
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	/** Legacy PDU (no EXT_ADV opt) so phones/HA can read it, non-connectable,
	 * pinned to the stable identity address. */
	const bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
		BT_LE_ADV_OPT_USE_IDENTITY,
		ADV_INTERVAL, ADV_INTERVAL, nullptr);
	err = bt_le_ext_adv_create(&adv_param, nullptr, &adv);
	if (err)
	{
		LOG_ERR("ext_adv_create failed (%d)", err);
		return err;
	}

	LOG_INF("BT ready, BTHome v2 broadcaster");
	return 0;
}

int bthome_publish(int moisture_percent, int32_t battery_mv, int battery_percent)
{
	if (moisture_percent < 0)
	{
		moisture_percent = 0;
	}
	if (moisture_percent > 100)
	{
		moisture_percent = 100;
	}
	if (battery_percent < 0)
	{
		battery_percent = 0;
	}
	if (battery_percent > 100)
	{
		battery_percent = 100;
	}

	++packet_counter;
	/** Alternate packets: odd (1st, 3rd, ...) -> name (no fw), even -> fw
	 * (no name). The 31-byte legacy advert can't hold both at once; name +
	 * fw are static so HA caches whichever it sees. */
	const bool with_name = (packet_counter % 2) == 1;

	BTHomePacket<31> packet;
	packet.add(BTHome::packet_id(packet_counter));
	packet.add(BTHome::moisture(static_cast<float>(moisture_percent)));
	packet.add(BTHome::battery(battery_percent));
	packet.add(BTHome::voltage(battery_mv / 1000.0f));
	if (!with_name)
	{
		packet.add(BTHome::firmware_version_u24(FW_VERSION));
	}

	const bt_data svc = BT_DATA(BT_DATA_SVC_DATA16, packet.serviceData(), static_cast<uint8_t>(packet.serviceDataSize()));
	const bt_data flags_ad = BT_DATA(BT_DATA_FLAGS, &flags, sizeof(flags));

	if (with_name)
	{
		const char *name = bt_get_name();
		const bt_data ad[] = {
			flags_ad,
			svc,
			BT_DATA(BT_DATA_NAME_COMPLETE, name, static_cast<uint8_t>(strlen(name))),
		};
		return advertise_once(ad, ARRAY_SIZE(ad));
	}

	const bt_data ad[] = {flags_ad, svc};
	return advertise_once(ad, ARRAY_SIZE(ad));
}
