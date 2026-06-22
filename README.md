# xXCYD-PokerXx

Five games for the ESP32-32E (1-USB) and 2USB CYD (Cheap Yellow Display) — classic 5-Card Draw Joker Poker, heads-up Texas Hold'em against xXSmokeXx (AI), a 3-reel poker-suit slot machine, **Blackjack**, and **War**.

[![Support on Patreon](https://img.shields.io/badge/Support-Patreon-orange)](https://www.patreon.com/c/xXQuantumSmokeXx)

## Screens

| Video Poker | Texas Hold'em | Slots |
|-------------|---------------|-------|
| ![Video Poker](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/Main-Screen.png) | ![Texas Hold'em](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/Texas-Hold'em.png) | ![Slots](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/Slot-Machine.png) |

| Blackjack | War | Settings |
|-----------|-----|----------|
| ![Blackjack](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/BlackJack.png) | ![War](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/War.png) | ![Settings](https://raw.githubusercontent.com/xXQuantumSmokeXx/xXCYD-PokerXx/main/Screenshots/Settings.png) |

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
- AI adapts to hand strength — calls/bluffs more, folds less against small bets
- 20 rotating AI status messages ("Rigging Algorithms...", "Thinks your soft...", etc.)
- Persistent chip stacks — survive power cycles and game mode switches

**Slots:**
- 3-reel slot machine with poker-suit symbols (♠♥♦♣★7)
- Single payline, 6 winning combinations from Any Pair (2×) to ★★★ Jackpot (100×)
- Variable bet (1/3/5/10 credits), gamble/double feature on wins
- Animated reel spin with staggered stops and deceleration
- On-screen paytable with all 6 payouts

**Blackjack:**
- Classic casino blackjack — player vs dealer, closest to 21 wins
- Hit, Stand, Double Down, and Split actions with right-panel buttons
- Dealer auto-play with animated card reveals (stands on all 17s)
- Natural blackjack pays 3:2, adjustable bet from 5–50 credits
- BET cycle button — tap to cycle bet amount (5→10→20→30→50→5)
- Split pairs (including Aces — one card per Ace hand)

**War:**
- The simplest card game — flip a card against the dealer, highest wins
- 52-card deck split 26–26, first to collect all 52 cards wins
- WAR! on ties — 3 cards burned face-down, 4th card face-up decides the winner
- Consecutive wars (up to 3) with accumulating pot — triple war splits the pot
- 5 credit entry fee, 25 credit payout for winning the full game
- Animated war sequence with timed burn-card reveals

**General:**
- 9 theme accent colors — saved to NVS
- Credit persistence across all three games — survives power cycles and deep sleep
- Tap theme name in the credits panel to cycle themes
- Power button (top-right) — tap for deep sleep, touch screen to wake
- RESET button in Hold'em resets all scores to defaults
- Mode toggle buttons: VIDEO POKER, HOLD'EM, SLOTS, BLACKJACK, WAR
- Serial screenshot capture via RGB332 protocol (`screenshot.py` included)
- Custom geometric card and symbol art — all drawn with TFT_eSPI primitives, no bitmaps

### Setup

| Board | Firmware File |
|-------|--------------|
| **ESP32-32E** (1-USB) | `CYD-Poker-1usb.bin` |
| **2USB** (all variants) | `CYD-Poker-2usb.bin` |

These are **merged flash images** — bootloader + partition table + application firmware combined into a single file. Flash at offset `0x00` with any ESP32 tool (esptool, ESP32 Flash Download Tool, BinForge, etc.).

**Direct flash:**
```bash
esptool.py --chip esp32 write_flash 0x0 CYD-Poker-1usb.bin
esptool.py --chip esp32 write_flash 0x0 CYD-Poker-2usb.bin
```

**Or via M5Launcher:** copy the `.bin` file onto a micro SD card (FAT32), insert into your CYD, launch [M5Launcher](https://github.com/bmorcelli/M5Launcher), select the firmware, and flash.

### First Boot (2USB)

On first boot, two calibration screens appear:

1. **Display calibration** — an asymmetric reference pattern (amber triangle, colored bracket, ring, crosshair, "T"). Tap to cycle through 8 display rotations. When the pattern looks correct, hold 2 seconds to confirm.

2. **Touch calibration** — corner crosshair targets with a live amber cursor that follows your finger. Tap to cycle through 4 touch-digitizer rotations. When the cursor accurately follows your finger, hold 2 seconds to confirm.

Calibration runs once and persists in NVS. To re-run it, send `M` (display) or `T` (touch) via serial, or clear NVS.

### Build

Build from source with PlatformIO:

```bash
# ESP32-32E (1-USB)
pio run --environment cyd_poker

# 2USB (all variants — LovyanGFX auto-detect)
pio run --environment cyd_poker_2usb
```

After a successful build, merged flashable images (`CYD-Poker-1usb.bin` / `CYD-Poker-2usb.bin`) are auto-generated at the project root.

### Credits

Originally inspired by [Jolly-Card-Poker-CYD](https://github.com/dzulidzan/Jolly-Card-Poker-CYD) by dzulidzan. Completely rewritten with custom card art, theming, and Texas Hold'em mode.

Calibration system ported from [xXCYD-WeatherXx](https://github.com/xXQuantumSmokeXx/xXCYD-Weather-StationXx).

Built by xXQuantum-SmokeXx, with development assistance from Codex & Claude Code.

UI design ported from the "QuantumSix" "VR project", originally developed in collaboration with "Six" & "Nova"...
