#include "calibration.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include "app_config.h"
#include "display_ui.h"

LOG_MODULE_REGISTER(calib, LOG_LEVEL_INF);

// ---------------------------------------------------------------------------
// Module state — written once by calib_init, then read-only
// ---------------------------------------------------------------------------

static SoilSensor *s_soil = nullptr;
static const gpio_dt_spec *s_btn = nullptr;
static k_work_delayable *s_resume = nullptr;

static const struct device *const s_log_uart = DEVICE_DT_GET(DT_NODELABEL(uart1));

static void uart_wake()
{
    if (device_is_ready(s_log_uart))
        pm_device_action_run(s_log_uart, PM_DEVICE_ACTION_RESUME);
}

static void uart_sleep()
{
    if (device_is_ready(s_log_uart))
        pm_device_action_run(s_log_uart, PM_DEVICE_ACTION_SUSPEND);
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------

#define CALIB_NVS_ID_DRY 1
#define CALIB_NVS_ID_WET 2

static struct nvs_fs calib_fs;
static bool calib_fs_ready = false;

static void nvs_load()
{
    const struct device *dev = FIXED_PARTITION_DEVICE(storage_partition);
    if (!device_is_ready(dev))
    {
        LOG_WRN("flash not ready, using compile-time defaults");
        return;
    }

    struct flash_pages_info page;
    if (flash_get_page_info_by_offs(dev, FIXED_PARTITION_OFFSET(storage_partition),
                                    &page) < 0)
    {
        LOG_WRN("flash page query failed");
        return;
    }

    calib_fs.flash_device = dev;
    calib_fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
    calib_fs.sector_size = page.size;
    calib_fs.sector_count = FIXED_PARTITION_SIZE(storage_partition) / page.size;

    if (nvs_mount(&calib_fs) < 0)
    {
        LOG_WRN("NVS mount failed, using compile-time defaults");
        return;
    }
    calib_fs_ready = true;

    int32_t dry = cfg::SOIL_MV_DRY;
    int32_t wet = cfg::SOIL_MV_WET;
    bool stored = false;
    if (nvs_read(&calib_fs, CALIB_NVS_ID_DRY, &dry, sizeof(dry)) == sizeof(dry))
    {
        stored = true;
    }
    if (nvs_read(&calib_fs, CALIB_NVS_ID_WET, &wet, sizeof(wet)) == sizeof(wet))
    {
        stored = true;
    }

    if (stored && dry != wet)
    {
        s_soil->set_calibration(dry, wet);
        LOG_INF("calibration loaded: dry=%d mV  wet=%d mV", dry, wet);
    }
    else if (stored)
    {
        LOG_WRN("invalid calibration in NVS (dry==wet=%d), clearing", dry);
        nvs_write(&calib_fs, CALIB_NVS_ID_DRY, &dry, 0);
        nvs_write(&calib_fs, CALIB_NVS_ID_WET, &wet, 0);
        LOG_INF("using defaults: dry=%d mV  wet=%d mV",
                cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
    }
    else
    {
        LOG_INF("no stored calibration, using defaults: dry=%d mV  wet=%d mV",
                cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
    }
}

static void nvs_save()
{
    if (!calib_fs_ready)
        return;
    int32_t dry = s_soil->dry_mv();
    int32_t wet = s_soil->wet_mv();
    nvs_write(&calib_fs, CALIB_NVS_ID_DRY, &dry, sizeof(dry));
    nvs_write(&calib_fs, CALIB_NVS_ID_WET, &wet, sizeof(wet));
}

// ---------------------------------------------------------------------------
// Calibration thread
// Hold 0.5–2 s  → WET   Hold 2–4 s → DRY
// Hold 4–6 s   → RESET  Hold 6 s+  → auto-cancel (returns to main)
// ---------------------------------------------------------------------------

constexpr int64_t CAL_MIN_HOLD_MS = 500;
constexpr int64_t CAL_PHASE_DRY_MS = 2000;
constexpr int64_t CAL_PHASE_RST_MS = 4000;
constexpr int64_t CAL_PHASE_CANCEL_MS = 6000;

enum class CalMode
{
    NONE,
    WET,
    DRY,
    RESET,
};

static K_SEM_DEFINE(s_ready, 0, 1);

static void calib_thread_fn(void *, void *, void *)
{
    k_sem_take(&s_ready, K_FOREVER);

    while (true)
    {
        // Wait for button press. gpio_pin_get_dt returns 1 when active (pressed).
        while (gpio_pin_get_dt(s_btn) == 0)
            k_msleep(20);

        int64_t t0 = k_uptime_get();
        CalMode phase = CalMode::NONE;

        k_work_cancel_delayable(s_resume);
        uart_wake();

        // Poll while held, updating display on each phase change.
        bool auto_cancelled = false;
        while (gpio_pin_get_dt(s_btn) != 0)
        {
            int64_t held = k_uptime_get() - t0;
            if (held >= CAL_PHASE_CANCEL_MS)
            {
                LOG_INF(">> auto-cancel");
                ui_exit_calibration();
                uart_sleep();
                k_work_reschedule(s_resume, K_NO_WAIT);
                while (gpio_pin_get_dt(s_btn) != 0)
                    k_msleep(20);
                auto_cancelled = true;
                break;
            }
            CalMode p = (held < CAL_MIN_HOLD_MS)    ? CalMode::NONE
                        : (held < CAL_PHASE_DRY_MS) ? CalMode::WET
                        : (held < CAL_PHASE_RST_MS) ? CalMode::DRY
                                                    : CalMode::RESET;
            if (p != phase)
            {
                phase = p;
                const char *lbl = (p == CalMode::WET)    ? "WET"
                                  : (p == CalMode::DRY)  ? "DRY"
                                  : (p == CalMode::RESET) ? "RESET"
                                                          : nullptr;
                if (lbl)
                {
                    LOG_INF(">> %s", lbl);
                    ui_show_calibration_step(lbl);
                }
            }
            k_msleep(50);
        }
        if (auto_cancelled)
            continue;

        // Short press — return to main loop.
        if (phase == CalMode::NONE)
        {
            uart_sleep();
            k_work_reschedule(s_resume, K_NO_WAIT);
            continue;
        }

        if (phase == CalMode::RESET)
        {
            s_soil->set_calibration(cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
            nvs_save();
            LOG_INF("reset: dry=%d mV  wet=%d mV — rebooting",
                    cfg::SOIL_MV_DRY, cfg::SOIL_MV_WET);
            sys_reboot(SYS_REBOOT_COLD);
        }

        // WET or DRY: one read with the normal power-on/settle/off cycle.
        const bool is_wet = (phase == CalMode::WET);
        LOG_INF("entered %s calibration", is_wet ? "wet" : "dry");

        int32_t mv;
        if (s_soil->sample_raw(&mv) != 0)
        {
            LOG_WRN("read failed, aborting");
            uart_sleep();
            k_work_reschedule(s_resume, K_NO_WAIT);
            continue;
        }

        if (is_wet)
            s_soil->set_calibration(s_soil->dry_mv(), mv);
        else
            s_soil->set_calibration(mv, s_soil->wet_mv());
        nvs_save();

        if (is_wet)
            LOG_INF("saved wet: %d mV  (dry=%d mV)", mv, s_soil->dry_mv());
        else
            LOG_INF("saved dry: %d mV  (wet=%d mV)", mv, s_soil->wet_mv());
        uart_sleep();

        ui_show_calibration_step(is_wet ? "WET" : "DRY", mv);
        k_work_reschedule(s_resume, K_SECONDS(3));
    }
}

K_THREAD_DEFINE(calib_tid, 4096, calib_thread_fn, NULL, NULL, NULL,
                K_PRIO_PREEMPT(5), 0, 0);

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int calib_init(SoilSensor &soil, const gpio_dt_spec &btn,
               k_work_delayable *resume_work)
{
    s_soil = &soil;
    s_btn = &btn;
    s_resume = resume_work;

    nvs_load();

    if (!gpio_is_ready_dt(&btn))
    {
        LOG_ERR("button GPIO not ready");
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(&btn, GPIO_INPUT);
    if (err < 0)
        return err;

    k_sem_give(&s_ready);
    LOG_INF("ready on pin %d", btn.pin);
    return 0;
}
