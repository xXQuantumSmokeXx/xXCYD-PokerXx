## v1.0.0 — CYD-Poker

Two poker games for the ESP32 Cheap Yellow Display — 5-Card Draw Joker Poker and heads-up Texas Hold'em against xXSmokeXx (AI).

### Board Support
| USB Version | Firmware File | Notes |
|------------|--------------|-------|
| **1-USB** (original, 1 port) | Use existing v1.0.0 release | Standard ILI9341 CYD |
| **2-USB** (newer, 2 ports) | `firmware-2usb.bin` | ILI9341, landscape + mirror Y, no color invert |

### 2-USB Configuration
The 2-USB board has the LCD physically flipped compared to the 1-USB version. This firmware uses:
- TFT_eSPI ILI9341 driver
- Display: setRotation(1) + MADCTL mirror Y, no invert
- Touch: 180° rotation with both axes flipped
- Environment: `cyd_poker` (TFT_eSPI ILI9341, CYD_USB_VERSION=2)

### Flashing
1. Download the correct `.bin` for your board
2. Use [M5Launcher](https://github.com/bmorcelli/M5Launcher) or esptool to flash
3. For esptool: `esptool.py --port COMx write_flash 0x0 firmware-2usb.bin`

### Built With
- PlatformIO + Espressif 32 / Arduino framework
- TFT_eSPI 2.5.43 by Bodmer
- XPT2046 Touchscreen by PaulStoffregen
