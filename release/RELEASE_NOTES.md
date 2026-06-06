## v1.0.5 — LovyanGFX 2USB Environment

**This release is for 2USB CYD boards only (USB-C + Micro USB).**

### What's New
- **New cyd_poker_2usb_lgfx** environment using LovyanGFX instead of TFT_eSPI
- LovyanGFX auto-detects the display controller (ILI9341 vs ST7789)
- Orientation test expanded from 5 to 8 rotation modes for LGFX builds
- All drawing code templated for dual-backend (TFT_eSPI + LovyanGFX)

### Firmware Files
| File | Environment | Display |
|------|------------|---------|
| firmware-2usb.bin | cyd_poker_2usb_st7789 | ST7789 (TFT_eSPI) |
| firmware-2usb-lgfx.bin | cyd_poker_2usb_lgfx | Auto-detect (LovyanGFX) |

### Existing Envs — Unchanged
- cyd_poker_2usb_st7789 — 2USB ST7789

### Try the LGFX build first on 2USB
Flash firmware-2usb-lgfx.bin to your 2USB board. If the display works, LovyanGFX correctly identified your panel.
