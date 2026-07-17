# Bundled fonts

`montserrat_40_digits.c` is a 1-bit bitmap subset of Montserrat Medium, generated with
`lv_font_conv`: only the fourteen glyphs the big value labels use ("0123456789% mV").
The full built-in `lv_font_montserrat_40` costs ~70 KB of flash; this subset ~3 KB --
the difference is what let the Matter image fit the code partition. The small text face
stays LVGL's built-in `lv_font_montserrat_16` (menus need the whole ASCII set).

Montserrat is © 2011 The Montserrat Project Authors, licensed under the **SIL Open Font
License 1.1** — full text in [`licenses/OFL-Montserrat.txt`](licenses/OFL-Montserrat.txt).
(These are unnamed bitmap glyph arrays embedded in firmware; no font name is shown to any
user. OFL requires the licence to accompany the font files — done here — not attribution
in the product UI.)

## Regenerating

```
npx lv_font_conv --font <lvgl>/scripts/built_in_font/Montserrat-Medium.ttf \
    --size 40 --bpp 1 --format lvgl --symbols "0123456789% mV" \
    --lv-include lvgl.h -o montserrat_40_digits.c
```

Adding a character to a value label means adding it to `--symbols` and regenerating --
a missing glyph renders as nothing, silently.
