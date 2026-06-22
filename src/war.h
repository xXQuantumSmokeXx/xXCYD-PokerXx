#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ── War constants ───────────────────────────────────────────────────────────
#define WAR_BET           5
#define WAR_PAYOUT        25      // 5x entry for winning the full game
#define WAR_MAX_DEPTH     3       // max consecutive wars (split pot after 3)

// ── Layout ──────────────────────────────────────────────────────────────────
#define WAR_DLR_CARD_X    40      // dealer card position
#define WAR_DLR_CARD_Y    60      // moved down 10px
#define WAR_PLY_CARD_X    (SCREEN_W - 40 - CARD_W)  // player card position
#define WAR_PLY_CARD_Y    60      // moved down 10px
#define WAR_LABEL_Y       25      // pile count labels, moved down 5px
#define WAR_RESULT_Y      160     // result text, moved down
#define WAR_BTN_Y         197     // action button
#define WAR_BTN_W         120
#define WAR_BTN_H         26

// War card positions (center area, between main cards) — slightly bigger
#define WAR_WAR_CARD_W    24
#define WAR_WAR_CARD_H    32
#define WAR_WAR_GAP       5
#define WAR_WAR_Y         145

// ── Game phases ─────────────────────────────────────────────────────────────
enum WarPhase : uint8_t {
    WAR_IDLE       = 0,   // waiting to start a game
    WAR_PLAYING    = 1,   // waiting for player to FLIP
    WAR_REVEAL     = 2,   // cards revealed, result shown
    WAR_WAR        = 3,   // war animation: burn 3, reveal 4th
    WAR_GAME_OVER  = 4    // game ended, payout awarded
};

// ── War state ───────────────────────────────────────────────────────────────
struct WarState {
    WarPhase phase;

    // Deck and piles: single array with start/end indices
    // Player owns deck[playerStart .. playerEnd-1]
    // Dealer owns deck[dealerStart .. dealerEnd-1]
    // Cards are drawn from the END (top of pile), added to START (bottom)
    uint8_t  deck[52];
    uint8_t  playerStart;
    uint8_t  playerEnd;
    uint8_t  dealerStart;
    uint8_t  dealerEnd;

    // Current face-up cards
    uint8_t  playerCard;
    uint8_t  dealerCard;

    // War state
    bool     inWar;
    uint8_t  warDepth;
    uint8_t  warPlayerBurn[3];  // 3 face-down burn cards (visible)
    uint8_t  warDealerBurn[3];
    uint8_t  warPlayerCard;     // 4th face-up card in war
    uint8_t  warDealerCard;
    uint8_t  warPot[40];        // accumulated cards during consecutive wars
    uint8_t  warPotCount;

    // Result
    int      lastResult;    // -1=dealer wins, 0=tie, 1=player wins
    int      roundsWon;
    int      roundsLost;
    unsigned long payout;

    char     resultMsg[28];
    unsigned long animTimer;
    uint8_t  animStep;      // 0=first burn, 1=second, 2=third, 3=reveal, 4=done
};

extern WarState g_war;

// ── Public API ──────────────────────────────────────────────────────────────
void warInit();
void warDraw(TFT_eSPI &d, unsigned long credits);
bool warTap(TFT_eSPI &d, int16_t tx, int16_t ty, unsigned long &credits);
bool warTick(TFT_eSPI &d, unsigned long credits);  // returns true if redraw needed
