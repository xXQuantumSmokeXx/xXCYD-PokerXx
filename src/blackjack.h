#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// ── Blackjack constants ─────────────────────────────────────────────────────
#define BJ_MIN_BET        5
#define BJ_MAX_BET        50
#define BJ_BET_STEP       5
#define BJ_CARD_W         52      // slightly bigger
#define BJ_CARD_H         60
#define BJ_CARD_GAP       58      // 52 + 6px gap
#define BJ_MAX_HAND       7       // max cards in a hand (extremely rare to need 7)

// ── Layout ──────────────────────────────────────────────────────────────────
#define BJ_DEALER_Y       24      // dealer cards row
#define BJ_PLAYER_Y       167     // player main hand row
#define BJ_SPLIT_Y        231     // split hand row
#define BJ_INFO_Y         94      // info text area
#define BJ_INFO_H         48

// Right-panel buttons (right-aligned, 5px text padding)
#define BJ_BTN_X          264
#define BJ_BTN_W          50
#define BJ_BTN_H          22
#define BJ_BTN_GAP        5

#define BJ_HIT_Y          24
#define BJ_STAND_Y        (BJ_HIT_Y    + BJ_BTN_H + BJ_BTN_GAP)
#define BJ_DOUBLE_Y       (BJ_STAND_Y  + BJ_BTN_H + BJ_BTN_GAP)
#define BJ_SPLIT_Y_BTN    (BJ_DOUBLE_Y + BJ_BTN_H + BJ_BTN_GAP)
#define BJ_DEAL_BTN_Y     (BJ_SPLIT_Y_BTN + BJ_BTN_H + BJ_BTN_GAP)
#define BJ_BET_BTN_Y      (BJ_DEAL_BTN_Y - BJ_BTN_H - BJ_BTN_GAP)  // bet cycle above DEAL

// Credits / bet display (left side, below info area, aligned with player cards)
#define BJ_CREDITS_Y      140     // left of player card row
#define BJ_BET_DISP_Y     155     // below credits

// ── Game phases ─────────────────────────────────────────────────────────────
enum BlackjackPhase : uint8_t {
    BJ_IDLE        = 0,   // waiting for deal, can adjust bet
    BJ_PLAYING     = 1,   // player's turn: HIT/STAND/DOUBLE/SPLIT
    BJ_DEALER_TURN = 2,   // dealer auto-plays
    BJ_HAND_OVER   = 3    // result shown, tap to continue
};

// ── Individual hand ─────────────────────────────────────────────────────────
struct BjHand {
    uint8_t cards[BJ_MAX_HAND];
    uint8_t count;
    bool    stood;
    bool    bust;
    bool    isBlackjack;    // natural 21 on first 2 cards
};

// ── Blackjack state ─────────────────────────────────────────────────────────
struct BlackjackState {
    BlackjackPhase phase;

    // Deck
    uint8_t  deck[52];
    uint8_t  deckPos;

    // Dealer
    BjHand   dealer;
    uint8_t  dealerRevealed;  // how many dealer cards are face-up (1 until dealer turn)

    // Player hands (hand[0] = main, hand[1] = split)
    BjHand   hands[2];
    uint8_t  handCount;       // 1 = no split, 2 = split active
    uint8_t  activeHand;      // 0 or 1 — which hand player is acting on

    // Betting
    unsigned long bet;        // current bet per hand

    // Payout tracker
    unsigned long payoutMain;
    unsigned long payoutSplit;

    // Result display
    char     resultMsg[36];

    // Dealer animation timer
    unsigned long animTimer;
    uint8_t  animStep;        // 0=reveal hole card, 1+=dealing extra cards
    bool     dealerDone;
};

extern BlackjackState g_bj;

// ── Public API ──────────────────────────────────────────────────────────────
void blackjackInit();
void blackjackDraw(TFT_eSPI &d, unsigned long credits);
bool blackjackTap(TFT_eSPI &d, int16_t tx, int16_t ty, unsigned long &credits);
bool blackjackTick(TFT_eSPI &d, unsigned long credits);  // returns true if redraw needed
