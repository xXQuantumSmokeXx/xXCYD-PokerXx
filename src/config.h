#pragma once

// ── Display (ILI9341 on HSPI) ─────────────────────────────────────────────
#define TFT_MOSI   13
#define TFT_MISO   12
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1
#define TFT_BL     21

// ── Touch (XPT2046 on VSPI) ───────────────────────────────────────────────
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
#define TOUCH_X_MIN  300
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX 3800

// ── Screen geometry (landscape, rotation 1) ───────────────────────────────
#define SCREEN_W   320
#define SCREEN_H   240

// ── Game constants ────────────────────────────────────────────────────────
#define MAX_JOKERS   1
#define BET          5
#define STARTING_CREDITS  100

// ── Layout ────────────────────────────────────────────────────────────────
// Paytable — left side
// Cards: 5 cards, 4 gaps of (CARD_GAP-CARD_W)=6px each, total span = 5*56 + 4*6 = 304
// Centered: (320-304)/2 = 8
#define PT_X            8
#define PT_Y            2
#define PT_LINE_H       9     // Font 1 line height (7px font + 2px gap)
#define PT_W            186
#define PT_H            (10 * PT_LINE_H)
#define PT_RX           (PT_X + PT_W)
#define PAYTABLE_X      PT_X

// Right panel
#define RIGHT_X         195
#define RIGHT_W         (SCREEN_W - RIGHT_X - 8)
#define CREDITS_CX      (RIGHT_X + RIGHT_W / 2)

// Action button
#define BTN_X           RIGHT_X + 13
#define BTN_W           90
#define BTN_Y           58
#define BTN_H           32

// Gamble buttons (phase 3)
#define GMBL_X          BTN_X
#define GMBL_W          BTN_W
#define GMBL_H          30
#define GMBL_Y0         46
#define GMBL_GAP        36

// Message bar — moves with cards
#define MSG_Y           118
#define MSG_H           16

// Cards — lower on screen
#define CARD_Y          142

// Power button (extreme top-right corner)
#define PWR_BTN_X       306
#define PWR_BTN_Y       10
#define PWR_BTN_R       7
#define CARD_W          56
#define CARD_H          82
#define CARD_GAP        62

// ── Colors (RGB565) ───────────────────────────────────────────────────────
#define COL_BG          0x0000
#define COL_WHITE       0xFFFF
#define COL_BLACK       0x0000
#define COL_RED         0xF800
#define COL_DARK_RED    0x9000
#define COL_GREEN       0x07E0
#define COL_DARK_GREEN  0x0300
#define COL_GOLD        0xFDA0
#define COL_PALE_YELLOW 0xFFEF
#define COL_LIGHT_GRAY  0xC618
#define COL_MID_GRAY    0x8410
#define COL_DIM_GRAY    0x4208
#define COL_HOLD_AMBER  0xA124
#define COL_WIN_BG      0xE000

// ── Paytable ──────────────────────────────────────────────────────────────
static const char* const WIN_NAMES[] = {
    "FIVE OF A KIND", "ROYAL FLUSH", "STRAIGHT FLUSH", "FOUR OF A KIND",
    "FULL HOUSE", "FLUSH", "STRAIGHT", "THREE OF A KIND",
    "TWO PAIR", "HIGH PAIR (J+)"
};
static const uint16_t PAYOUTS[] = {1000, 500, 100, 40, 10, 7, 5, 3, 2, 1};

// ── Texas Hold'em layout ──────────────────────────────────────────────────
// AI cards TOP → community cards CENTER → player cards BOTTOM → buttons on sides
#define HM_INFO_Y        2
#define HM_AI_Y          24      // AI cards (top section)
#define HM_AI_LABEL_Y    20      // xXSmokeXx label above AI cards
#define HM_COMM_Y        92      // 5 community cards (center)
#define HM_STAGE_Y       158     // stage label below community
#define HM_STATUS_Y      170     // AI status message
#define HM_HOLE_Y        188     // player hole cards (bottom)
#define HM_SIDE_BTN_Y    192     // side buttons (aligned with player cards)
#define HM_SIDE_BTN_W    52
#define HM_SIDE_BTN_H    56      // tall side button
#define HCARD_GAP        48      // 42px card + 6px gap
#define HM_BTN_H         24

// Mode toggle button (same style as action button)
#define MODE_BTN_X       BTN_X
#define MODE_BTN_W       BTN_W
#define MODE_BTN_Y       58    // top of right panel, where action button was
#define MODE_BTN_H       32   // same height as BTN_H
