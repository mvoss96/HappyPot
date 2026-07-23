# Bundled fonts

Generated 1-bit bitmap subsets of Montserrat Medium (`lv_font_conv`), **strong-autohinted**:
the panel is 1-bit, so the built-in 4bpp fonts' antialiasing just thresholds away at render
time -- hinting the outlines onto the pixel grid at generation time is what keeps stems
straight and weights even.

| File | Size | Glyphs | Used for |
|------|------|--------|----------|
| `montserrat_40_digits.c` | 40 px | `0123456789% mV` | the big value labels |
| `montserrat_16_ui.c` | 16 px | ASCII 0x20-0x7F | menus, hints, meta text |

The full built-in Montserrat 40 + 16 cost ~95 KB of flash; these subsets a fraction of it --
the difference is what lets the Matter image fit the code partition.

Montserrat is © 2011 The Montserrat Project Authors, licensed under the **SIL Open Font
License 1.1** — full text in [`licenses/OFL-Montserrat.txt`](licenses/OFL-Montserrat.txt).
(These are unnamed bitmap glyph arrays embedded in firmware; no font name is shown to any
user. OFL requires the licence to accompany the font files — done here — not attribution
in the product UI.)

## Regenerating

```
npx lv_font_conv --font <lvgl>/scripts/built_in_font/Montserrat-Medium.ttf \
    --size 40 --bpp 1 --format lvgl --symbols "0123456789% mV" \
    --autohint-strong --lv-include lvgl.h -o montserrat_40_digits.c

npx lv_font_conv --font <lvgl>/scripts/built_in_font/Montserrat-Medium.ttf \
    --size 16 --bpp 1 --format lvgl --range 0x20-0x7F \
    --autohint-strong --lv-include lvgl.h -o montserrat_16_ui.c
```

Adding a character to a value label means adding it to `--symbols` and regenerating --
a missing glyph renders as nothing, silently.
