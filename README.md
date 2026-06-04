# xXCYD-PokerXx

Two poker games for the ESP32 Cheap Yellow Display — classic 5-Card Draw Joker Poker and heads-up Texas Hold'em against xXSmokeXx (AI).

[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-orange)](https://www.patreon.com/c/xXQuantumSmokeXx)

## Screens

| Video Poker | Texas Hold'em |
|-------------|---------------|
| ![Video Poker](VideoPoker.png) | ![Texas Hold'em](Holdem.png) |

### Features

**Video Poker (5-Card Draw):**
- Classic Joker Poker with one wild joker — 10 hand rankings up to Five of a Kind
- Tap cards to hold, draw to replace, gamble/double feature on wins
- Paytable displayed on-screen with all 10 payouts
- Auto-hold strategy on the initial deal

**Texas Hold'em:**
- Heads-up against xXSmokeXx AI with fixed blinds (2/5)
- Full betting rounds: pre-flop → flop → turn → river
- Fold, Check/Call, and Raise actions with side buttons
- AI evaluates hand strength and occasionally bluffs
- Rotating AI status messages ("Rigging Algorithms...", "Consulting the void...", etc.)
- Persistent chip stacks — survive power cycles and game mode switches

**General:**
- 9 theme accent colors (CYAN, GREEN, RED, ORANGE, YELLOW, GRAY, PURPLE, PINK, WHITE) — saved to NVS
- Credit persistence — score survives power cycles and deep sleep
- Tap theme name in the credits panel to cycle themes
- Power button (top-right) — tap for deep sleep, touch screen to wake
- RESET button in Hold'em — reset all scores to defaults
- Mode toggle button switches between Video Poker and Texas Hold'em
- Serial screenshot capture via RGB332 protocol (compatible with xXCYD-ScreenCaptureXx)
- Custom geometric card art — all drawn with TFT_eSPI primitives, no bitmaps

### Setup

Flash `firmware.bin` from the latest release — no SD card or WiFi required.

### Build

Build from source with PlatformIO:

```bash
pio run
```

The generated firmware is written to `.pio/build/cyd_poker/firmware.bin`.

### Hardware

- ESP32 (Cheap Yellow Display — CYD)
- ILI9341 240×320 TFT (landscape orientation)
- XPT2046 touch controller

Same pinout as the CYD-Weather project.

### Credits

Originally inspired by [Jolly-Card-Poker-CYD](https://github.com/dzulidzan/Jolly-Card-Poker-CYD) by dzulidzan. Completely rewritten with custom card art, theming, and Texas Hold'em mode.

Built by xXQuantum-SmokeXx, with development assistance from Claude Code.
