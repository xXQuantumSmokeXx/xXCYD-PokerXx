## v1.0.2 — Optimization Release

Code cleanup and bug fixes for both 1-USB and 2-USB CYD boards.

### Fixes
- **Hold'em blinds double-charge**: Removed premature `collectBetsToPot()` in `holdemNewHand()` that was charging BB posters twice during preflop betting
- **Hold'em overlapping buttons**: Removed dead duplicate "5-CARD DRAW" button drawn underneath "VIDEO POKER" in the Hold'em screen

### Optimizations
- Extracted hardcoded Hold'em back-button coordinates to named constants (`HM_BACK_X/Y/W/H` in `config.h`)
- Cleaned up commented-out dead code in legacy `main.ino`
- Added deprecation banner to legacy `main.ino` pointing to `src/` as active code

### Board Support
| USB Version | Firmware File | Driver |
|------------|--------------|--------|
| **1-USB** (ESP32-32E) | `cyd-poker-1USB.bin` | ILI9341 |
| **2-USB** (2 ports) | `cyd-poker-2USB.bin` | ILI9341 |

### 2-USB Configuration
- TFT_eSPI ILI9341 driver
- Display: setRotation(1) + MADCTL mirror Y, no invert
- Touch: 180° rotation with both axes flipped
- Environment: `cyd_poker_2usb` (ILI9341, CYD_USB_VERSION=2)

### Flashing
1. Download the correct `.bin` for your board
2. Use [M5Launcher](https://github.com/bmorcelli/M5Launcher) or esptool to flash
3. For esptool: `esptool.py --port COMx write_flash 0x0 firmware.bin`

### Built With
- PlatformIO + Espressif 32 / Arduino framework
- TFT_eSPI 2.5.43 by Bodmer
- XPT2046 Touchscreen by PaulStoffregen
