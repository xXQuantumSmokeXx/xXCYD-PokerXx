#pragma once
#include <Arduino.h>
#include "config.h"

// ── Hold'em constants ─────────────────────────────────────────────────────
#define HOLDEM_SB        2     // small blind
#define HOLDEM_BB        5     // big blind
#define HOLDEM_STARTING_STACK  200

// Card sizes for Hold'em
#define HCARD_W          40    // hole cards (player + AI)
#define HCARD_H          52
#define HCCARD_W         56    // community cards (same as main game)
#define HCCARD_H         82

// ── Game stages ────────────────────────────────────────────────────────────
enum HoldemStage : uint8_t {
    HM_IDLE = 0,      // waiting to deal
    HM_PREFLOP,       // 2 hole cards dealt, first betting round
    HM_FLOP,          // 3 community cards dealt
    HM_TURN,          // 4th community card dealt
    HM_RIVER,         // 5th community card dealt
    HM_SHOWDOWN,      // reveal cards, determine winner
    HM_HAND_OVER      // pot awarded, ready for next hand
};

// ── AI status messages ─────────────────────────────────────────────────────
#define HM_STATUS_COUNT  10
extern const char* g_hmStatuses[HM_STATUS_COUNT];

// ── Hold'em state ──────────────────────────────────────────────────────────
struct HoldemState {
    // Game flow
    HoldemStage  stage;
    bool         playerDealer;     // true = player is dealer this hand
    uint8_t      statusIdx;        // rotating AI status message index
    unsigned long statusTimer;     // when to cycle to next status

    // Deck
    uint8_t  deck[52];            // shuffled deck indices (0-51)
    uint8_t  deckPos;             // current draw position

    // Community cards (0-4 revealed as stages progress)
    uint8_t  community[5];
    uint8_t  communityRevealed;   // 0, 3, 4, or 5 cards face-up

    // Player
    uint8_t  playerCards[2];      // hole cards
    unsigned long playerStack;
    unsigned long playerBet;      // amount player has bet this round
    bool     playerFolded;
    bool     playerActed;         // has acted this betting round

    // AI
    uint8_t  aiCards[2];
    unsigned long aiStack;
    unsigned long aiBet;
    bool     aiFolded;

    // Pot & betting
    unsigned long pot;
    unsigned long currentBet;     // the bet to match (0 if no raise yet)
    bool     bettingRoundDone;    // both players have acted

    // Last actions (for displaying what happened)
    char     lastAction[32];       // final result / player action
    char     aiLastAction[32];     // what the AI did (fold/raise/call + amount)
};

extern HoldemState g_hm;

// ── Functions ──────────────────────────────────────────────────────────────
void holdemInit();                 // reset stacks, shuffle deck
void holdemNewHand();              // post blinds, deal hole cards
void holdemPlayerAction(uint8_t action);  // 0=fold, 1=check/call, 2=raise
void holdemNextStage();            // advance to flop/turn/river/showdown
void holdemAIAct();                // AI makes a decision
const char* holdemAIStatus();      // get current AI status message
int  holdemEvaluateBest(const uint8_t* cards7);  // best hand of 7 cards → win index
void holdemDetermineWinner();      // compare hands, award pot
