# Vendored drivers

## `ssd16xx.c` + `ssd16xx_regs.h`

Vendored from `zephyr/drivers/display/ssd16xx.c`, NCS v3.3.0
(Zephyr 4.3.99, commit `fd9204a02d52`).

Build wiring:
- `CMakeLists.txt` compiles `src/drivers/ssd16xx.c`.
- `prj.conf` sets `CONFIG_SSD16XX=n` so this copy claims the
  `solomon,ssd1681` DT compatible instead of the in-tree driver.

### Changes vs. upstream

1. **PM_DEVICE action callback** (`ssd16xx_pm_action`), registered via
   `PM_DEVICE_DT_DEFINE` in the per-instance macro.

2. **SUSPEND** sends `SSD16XX_CMD_SLEEP_MODE` + `SSD16XX_SLEEP_MODE_DSM`
   (`0x10 0x01`, deep sleep mode 1). Panel drops to ~1 uA; image stays
   latched.

3. **RESUME** (`ssd16xx_resume`): HW reset via `mipi_dbi_reset`,
   invalidate `data->profile`, call `ssd16xx_set_profile(FULL)` to
   re-upload LUT + `ENTRY_MODE`. Skips the `clear_cntlr_mem` +
   `update_display` that `ssd16xx_controller_init` does on cold boot,
   so the latched image survives wake.

4. **`update_display` keeps analog/clock on after partial refreshes.**
   Upstream always appends `DISABLE_ANALOG | DISABLE_CLK`; that causes
   a full waveform ramp on every partial refresh, which flashes the
   panel white. Our version only disables them after full refreshes.

### Updating

When bumping NCS, diff against the new upstream and re-apply the four
changes:

```powershell
git diff --no-index `
    C:/ncs/<version>/zephyr/drivers/display/ssd16xx.c `
    c:/Repos/nrf/HappyPot/src/drivers/ssd16xx.c
```

If upstream adds PM support, delete this directory, drop the
`src/drivers/ssd16xx.c` line from `CMakeLists.txt`, and set
`CONFIG_SSD16XX=y`.
