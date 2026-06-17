#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ── Slot symbols ───────────────────────────────────────────────────────────
enum SlotSymbol : uint8_t {
    SYM_CLUB    = 0,   // ♣ — most common, lowest pay
    SYM_DIAMOND = 1,   // ♦
    SYM_HEART   = 2,   // ♥
    SYM_SPADE   = 3,   // ♠ — highest suit pay
    SYM_WILD    = 4,   // ★ — substitutes for any suit, rarest
    SYM_SEVEN   = 5,   // 7 — second highest pay
    SYM_COUNT   = 6
};

// ── Spin state ─────────────────────────────────────────────────────────────
enum SlotsPhase : uint8_t {
    SLOT_IDLE     = 0,   // waiting for spin
    SLOT_SPINNING = 1,   // reels animating
    SLOT_EVALUATE = 2    // win/loss display, collect/gamble
};

// ── Layout constants ───────────────────────────────────────────────────────
#define SLOT_REEL_W       75
#define SLOT_REEL_H       100    // 3 symbols visible (~33px each)
#define SLOT_REEL_GAP     10
#define SLOT_REEL_X0      37     // (320 - 3*75 - 2*10) / 2
#define SLOT_REEL_Y       39
#define SLOT_SYM_H        33     // symbol cell height
#define SLOT_PAYLINE_Y    (SLOT_REEL_Y + SLOT_REEL_H / 2)  // center of middle symbol
#define SLOT_PAY_Y        152    // paytable top (below reels)
#define SLOT_BTN_Y        202    // bet + spin buttons (10px from bottom)
#define SLOT_BACK_Y       224    // VIDEO POKER back button
#define SLOT_BACK_H       14

#define SLOT_REEL_LENGTH  16
#define SLOT_BET_COUNT    4
#define SLOT_VISIBLE      3      // symbols visible per reel

// ── Reel state ─────────────────────────────────────────────────────────────
struct ReelState {
    int16_t      pos;           // strip index at top of visible window (0..15)
    bool         spinning;
    unsigned long stopAt;       // millis() when this reel starts decelerating
    uint8_t      stopTarget;    // symbol index to land on at payline (center)
    unsigned long lastAdvance;  // millis() of last position tick
    int          tickMs;        // ms between position advances (increases during decel)
    int          decelStep;     // 0=fast spin, 1-4=decelerating, 5=stopped
};

// ── Slots state ────────────────────────────────────────────────────────────
struct SlotsState {
    SlotsPhase phase;
    ReelState  reels[3];
    uint8_t    betIdx;         // 0-3 → 1, 3, 5, 10
    unsigned long winAmount;   // payout from last spin (0 = loss)
    char       winLabel[20];   // e.g. "♠♠♠ 3 of a kind!"
    bool       paylineFlash;   // toggle for win flash animation
    unsigned long flashTimer;

    // Gamble sub-state (when phase==SLOT_EVALUATE and winAmount > 0)
    bool       gambling;       // true = showing COLLECT/LOW/HIGH buttons
    unsigned long gambleAmount; // current gamble pot
};

extern SlotsState g_slots;

// ── Bet values ─────────────────────────────────────────────────────────────
extern const uint8_t SLOT_BETS[SLOT_BET_COUNT];

// ── Public API ─────────────────────────────────────────────────────────────
void slotsInit();
void slotsDraw(TFT_eSPI &tft, unsigned long credits);
bool slotsTap(TFT_eSPI &tft, int16_t tx, int16_t ty);
void slotsAnimate(TFT_eSPI &tft, unsigned long credits);
bool slotsIsSpinning();
