# xXCYD-PokerXx

Two poker games for the ESP32-32E and 2USB CYD (Cheap Yellow Display) — classic 5-Card Draw Joker Poker and heads-up Texas Hold'em against xXSmokeXx (AI).

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

### Setup (M5Launcher)

1. Download the correct firmware from the [Releases](https://github.com/xXQuantumSmokeXx/xXCYD-PokerXx/releases) page.
2. Copy the `.bin` file onto a micro SD card (FAT32).
3. Insert the SD card into your CYD and power it on.
4. Launch [M5Launcher](https://github.com/bmorcelli/M5Launcher), select the firmware, and flash.

### Credits

Originally inspired by [Jolly-Card-Poker-CYD](https://github.com/dzulidzan/Jolly-Card-Poker-CYD) by dzulidzan. Completely rewritten with custom card art, theming, and Texas Hold'em mode.

Built by xXQuantum-SmokeXx, with development assistance from Claude Code.

Check out my other project: [xXCYD-WeatherXx](https://github.com/xXQuantumSmokeXx/xXCYD-Weather-StationXx)
