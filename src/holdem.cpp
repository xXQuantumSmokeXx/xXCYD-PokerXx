#include "holdem.h"
#include "cards.h"

// ── AI status messages ─────────────────────────────────────────────────────
const char* g_hmStatuses[HM_STATUS_COUNT] = {
    "Thinking...",
    "Rigging Algorithms...",
    "Counting cards...",
    "Consulting the void...",
    "Calculating odds...",
    "Bribing the dealer...",
    "Reading your soul...",
    "Is sweating...",
    "Flipping a coin...",
    "Asking ChatGPT..."
};

HoldemState g_hm;

// ── Init ───────────────────────────────────────────────────────────────────

void holdemInit() {
    g_hm.stage       = HM_IDLE;
    g_hm.playerStack = HOLDEM_STARTING_STACK;
    g_hm.aiStack     = HOLDEM_STARTING_STACK;
    g_hm.playerDealer = true;
    g_hm.statusIdx   = 0;
    g_hm.statusTimer = 0;
}

// ── Deck shuffle ───────────────────────────────────────────────────────────

static void shuffleDeck() {
    for (int i = 0; i < 52; i++) g_hm.deck[i] = i;
    for (int i = 51; i > 0; i--) {
        int j = random(i + 1);
        uint8_t t = g_hm.deck[i];
        g_hm.deck[i] = g_hm.deck[j];
        g_hm.deck[j] = t;
    }
    g_hm.deckPos = 0;
}

static uint8_t drawCard() {
    return g_hm.deck[g_hm.deckPos++];
}

// Convert deck index (0-51) to our card encoding (rank*4 + suit)
static uint8_t deckToCard(uint8_t d) {
    uint8_t rank = (d % 13) + 1;  // 1=A, 2-10, 11=J, 12=Q, 13=K (no joker in Hold'em)
    uint8_t suit = d / 13;        // 0=C, 1=D, 2=H, 3=S
    return suit + rank * 4;
}

// ── Pot helper ─────────────────────────────────────────────────────────────

static void collectBetsToPot() {
    g_hm.pot += g_hm.playerBet + g_hm.aiBet;
    g_hm.playerBet = 0;
    g_hm.aiBet = 0;
}

// ── New hand ───────────────────────────────────────────────────────────────

void holdemNewHand() {
    // Reset hand state
    g_hm.stage            = HM_PREFLOP;
    g_hm.communityRevealed = 0;
    g_hm.playerFolded     = false;
    g_hm.aiFolded          = false;
    g_hm.playerBet         = 0;
    g_hm.aiBet             = 0;
    g_hm.currentBet        = 0;
    g_hm.pot               = 0;
    g_hm.bettingRoundDone  = false;
    g_hm.lastAction[0]     = 0;
    g_hm.aiLastAction[0]   = 0;

    // Rebuy for broke players
    if (g_hm.playerStack < HOLDEM_BB) {
        g_hm.playerStack = HOLDEM_STARTING_STACK;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "You rebuy for %d", HOLDEM_STARTING_STACK);
    }
    if (g_hm.aiStack < HOLDEM_BB) {
        g_hm.aiStack = HOLDEM_STARTING_STACK;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "xXSmokeXx rebuys %d (rigged!)", HOLDEM_STARTING_STACK);
    }

    shuffleDeck();

    // Post blinds
    if (g_hm.playerDealer) {
        // Player is dealer → posts SB, AI posts BB
        unsigned long sb = (unsigned long)HOLDEM_SB < g_hm.playerStack ? HOLDEM_SB : g_hm.playerStack;
        unsigned long bb = (unsigned long)HOLDEM_BB < g_hm.aiStack ? HOLDEM_BB : g_hm.aiStack;
        g_hm.playerStack -= sb;  g_hm.playerBet = sb;
        g_hm.aiStack     -= bb;  g_hm.aiBet     = bb;
        g_hm.currentBet = bb;
    } else {
        // AI is dealer → posts SB, player posts BB
        unsigned long sb = (unsigned long)HOLDEM_SB < g_hm.aiStack ? HOLDEM_SB : g_hm.aiStack;
        unsigned long bb = (unsigned long)HOLDEM_BB < g_hm.playerStack ? HOLDEM_BB : g_hm.playerStack;
        g_hm.aiStack     -= sb;  g_hm.aiBet     = sb;
        g_hm.playerStack -= bb;  g_hm.playerBet = bb;
        g_hm.currentBet = bb;
    }
    // Blinds stay in playerBet/aiBet for the preflop betting round.
    // They will be moved to pot by collectBetsToPot() in holdemNextStage()
    // when the preflop round ends. This ensures toCall calculations are correct.

    // Deal 2 hole cards to each
    g_hm.playerCards[0] = deckToCard(drawCard());
    g_hm.aiCards[0]     = deckToCard(drawCard());
    g_hm.playerCards[1] = deckToCard(drawCard());
    g_hm.aiCards[1]     = deckToCard(drawCard());

    // Burn card before community
    drawCard();

    // Pre-deal community cards (face down until revealed)
    g_hm.community[0] = deckToCard(drawCard());
    g_hm.community[1] = deckToCard(drawCard());
    g_hm.community[2] = deckToCard(drawCard());
    g_hm.community[3] = deckToCard(drawCard());
    g_hm.community[4] = deckToCard(drawCard());

    g_hm.playerActed = (g_hm.playerDealer);  // BB acts last pre-flop

    // AI acts first pre-flop if player is dealer
    if (g_hm.playerDealer) {
        g_hm.playerActed = false;
    } else {
        g_hm.playerActed = true;  // player is BB, AI acts first
    }
}

// ── AI action ──────────────────────────────────────────────────────────────

void holdemAIAct() {
    if (g_hm.aiFolded || g_hm.playerFolded) return;

    // Evaluate hand strength (simple heuristic)
    uint8_t cards7[7];
    cards7[0] = g_hm.aiCards[0];
    cards7[1] = g_hm.aiCards[1];
    for (int i = 0; i < g_hm.communityRevealed; i++)
        cards7[2 + i] = g_hm.community[i];
    int aiWinIdx = holdemEvaluateBest(cards7);

    unsigned long toCall = g_hm.currentBet - g_hm.aiBet;

    // Decision based on hand strength + randomness
    bool strongHand = (aiWinIdx <= 4);   // full house or better
    bool mediumHand = (aiWinIdx <= 7);   // three of a kind or better
    bool weakHand   = (aiWinIdx > 8);    // worse than two pair

    // Random element for unpredictability
    int rng = random(100);

    if (strongHand) {
        // Raise 60% of time, call 40%
        if (rng < 60 && g_hm.aiStack > g_hm.currentBet * 2) {
            unsigned long raise = g_hm.currentBet * 2;
            if (raise > g_hm.aiStack) raise = g_hm.aiStack;
            g_hm.aiStack -= (raise - g_hm.aiBet);
            g_hm.aiBet = raise;
            g_hm.currentBet = raise;
            g_hm.playerActed = false;
            snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "raises to %lu", raise);
        } else {
            g_hm.aiStack -= toCall;
            g_hm.aiBet += toCall;
            if (toCall > 0)
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "calls %lu", toCall);
            else
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "checks");
        }
    } else if (mediumHand) {
        // Call 70%, raise 20%, fold 10%
        if (rng < 70) {
            if (toCall > 0) {
                g_hm.aiStack -= toCall;
                g_hm.aiBet += toCall;
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "calls %lu", toCall);
            } else {
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "checks");
            }
        } else if (rng < 90 && g_hm.aiStack > g_hm.currentBet * 2) {
            unsigned long raise = g_hm.currentBet * 2;
            if (raise > g_hm.aiStack) raise = g_hm.aiStack;
            g_hm.aiStack -= (raise - g_hm.aiBet);
            g_hm.aiBet = raise;
            g_hm.currentBet = raise;
            g_hm.playerActed = false;
            snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "raises to %lu", raise);
        } else {
            g_hm.aiFolded = true;
            snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "folds");
        }
    } else {
        // Weak hand: fold 50%, call/bluff 50%
        if (rng < 50 && toCall > 0) {
            g_hm.aiFolded = true;
            snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "folds");
        } else {
            if (toCall > 0) {
                g_hm.aiStack -= toCall;
                g_hm.aiBet += toCall;
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "calls %lu", toCall);
            } else {
                snprintf(g_hm.aiLastAction, sizeof(g_hm.aiLastAction), "checks");
            }
        }
    }
}

// ── Player action ──────────────────────────────────────────────────────────

void holdemPlayerAction(uint8_t action) {
    if (g_hm.playerFolded) return;

    unsigned long toCall = g_hm.currentBet - g_hm.playerBet;

    switch (action) {
        case 0: // Fold
            g_hm.playerFolded = true;
            snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "You fold");
            holdemDetermineWinner();
            return;

        case 1: // Check / Call
            if (toCall > 0) {
                if (toCall > g_hm.playerStack) toCall = g_hm.playerStack;
                g_hm.playerStack -= toCall;
                g_hm.playerBet += toCall;
            }
            snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), toCall > 0 ? "You call" : "You check");
            break;

        case 2: // Raise (2x current bet)
            {
                unsigned long raise = g_hm.currentBet * 2;
                if (raise == 0) raise = HOLDEM_BB;
                if (raise > g_hm.playerStack) raise = g_hm.playerStack;
                unsigned long toRaise = raise - g_hm.playerBet;
                g_hm.playerStack -= toRaise;
                g_hm.playerBet = raise;
                g_hm.currentBet = raise;
                g_hm.playerActed = true;
                snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "You raise to %lu", raise);

                // AI responds to raise
                holdemAIAct();
                if (g_hm.stage == HM_HAND_OVER) return;
            }
            break;
    }

    g_hm.playerActed = true;

    // If both acted, move to next stage
    if (!g_hm.aiFolded) {
        holdemAIAct();
    }

    if (g_hm.stage != HM_HAND_OVER) {
        holdemNextStage();
    }
}

// ── Next stage ─────────────────────────────────────────────────────────────

void holdemNextStage() {
    // If someone folded, jump to showdown
    if (g_hm.playerFolded || g_hm.aiFolded) {
        holdemDetermineWinner();
        return;
    }

    // Collect bets into pot, reset for new round
    collectBetsToPot();
    g_hm.currentBet = 0;
    g_hm.playerActed = false;

    switch (g_hm.stage) {
        case HM_PREFLOP:
            g_hm.stage = HM_FLOP;
            g_hm.communityRevealed = 3;
            break;
        case HM_FLOP:
            g_hm.stage = HM_TURN;
            g_hm.communityRevealed = 4;
            break;
        case HM_TURN:
            g_hm.stage = HM_RIVER;
            g_hm.communityRevealed = 5;
            break;
        case HM_RIVER:
            holdemDetermineWinner();
            return;
        default:
            break;
    }
}

// ── Hand evaluation (best 5 of 7) ──────────────────────────────────────────

int holdemEvaluateBest(const uint8_t* cards7) {
    // Try all 21 combinations of 5 cards from 7 and find the best hand
    int bestWin = 99;  // higher = worse
    uint8_t combo[5];
    int combos[21][5] = {
        {0,1,2,3,4},{0,1,2,3,5},{0,1,2,3,6},{0,1,2,4,5},{0,1,2,4,6},
        {0,1,2,5,6},{0,1,3,4,5},{0,1,3,4,6},{0,1,3,5,6},{0,1,4,5,6},
        {0,2,3,4,5},{0,2,3,4,6},{0,2,3,5,6},{0,2,4,5,6},{0,3,4,5,6},
        {1,2,3,4,5},{1,2,3,4,6},{1,2,3,5,6},{1,2,4,5,6},{1,3,4,5,6},
        {2,3,4,5,6}
    };

    for (int c = 0; c < 21; c++) {
        for (int i = 0; i < 5; i++) combo[i] = cards7[combos[c][i]];

        // Evaluate this 5-card hand (same logic as main game, no jokers)
        uint8_t numbs[14] = {0};
        uint8_t suits[4] = {0};
        uint8_t minimum = 13, maximum = 2;
        uint8_t count = 1, rank = 0;
        uint8_t pair1 = 0, pair2 = 0;

        for (int i = 0; i < 5; i++) {
            uint8_t r = cardRank(combo[i]);
            uint8_t s = cardSuit(combo[i]);
            numbs[r]++;
            suits[s]++;
            if (r > 1 && r < minimum) minimum = r;
            if (r > maximum) maximum = r;
        }

        for (int i = 1; i <= 13; i++) {
            if (numbs[i] > count) { count = numbs[i]; rank = i; }
            if (numbs[(i % 13) + 1] == 2) { pair2 = pair1; pair1 = (i % 13) + 1; }
        }

        bool flush = false;
        for (int i = 0; i < 4; i++) if (suits[i] >= 5) flush = true;

        bool straight = false;
        if (count == 1) {
            if (numbs[1] == 1 && maximum <= 5) straight = true;
            else if (numbs[1] == 1 && minimum >= 10) straight = true;
            else if (maximum - minimum < 5) straight = true;
        }

        int w = -1;
        if      (count >= 4)                                      w = 3;  // 4 of kind
        else if (straight && flush && maximum >= 10 &&
                 ((maximum - minimum < 5 && numbs[1]==0) || (minimum>=10 && numbs[1]==1))) w = 1;
        else if (straight && flush)                               w = 2;
        else if (count == 3 && pair1)                             w = 4;  // full house
        else if (flush)                                           w = 5;
        else if (straight)                                        w = 6;
        else if (count == 3)                                      w = 7;
        else if (pair1 && pair2)                                  w = 8;
        else if (pair1 > 10 || pair1 == 1)                        w = 9;
        // else: high card (no win)

        if (w >= 0 && w < bestWin) bestWin = w;
    }
    return bestWin;
}

// ── Determine winner ───────────────────────────────────────────────────────

void holdemDetermineWinner() {
    // Collect any outstanding bets before awarding pot
    collectBetsToPot();

    g_hm.stage = HM_HAND_OVER;
    g_hm.communityRevealed = 5;  // show all community cards

    if (g_hm.playerFolded) {
        // AI wins by fold
        g_hm.aiStack += g_hm.pot;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "You folded - Smoke wins %lu", g_hm.pot);
        g_hm.pot = 0;
        return;
    }
    if (g_hm.aiFolded) {
        // Player wins by fold
        g_hm.playerStack += g_hm.pot;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "Smoke folded - You win %lu!", g_hm.pot);
        g_hm.pot = 0;
        return;
    }

    // Showdown: compare best 5-of-7 hands
    uint8_t pCards[7], aCards[7];
    pCards[0] = g_hm.playerCards[0]; pCards[1] = g_hm.playerCards[1];
    aCards[0] = g_hm.aiCards[0];     aCards[1] = g_hm.aiCards[1];
    for (int i = 0; i < 5; i++) {
        pCards[2+i] = g_hm.community[i];
        aCards[2+i] = g_hm.community[i];
    }

    int playerBest = holdemEvaluateBest(pCards);
    int aiBest     = holdemEvaluateBest(aCards);

    // Normalize: -1 (no hand) is worse than any valid hand (0-9)
    int p = (playerBest < 0) ? 99 : playerBest;
    int a = (aiBest < 0)     ? 99 : aiBest;

    if (p < a) {
        // Player wins
        g_hm.playerStack += g_hm.pot;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "You win %lu!", g_hm.pot);
    } else if (a < p) {
        // AI wins
        g_hm.aiStack += g_hm.pot;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "xXSmokeXx wins %lu!", g_hm.pot);
    } else {
        // Tie — split pot
        unsigned long half = g_hm.pot / 2;
        g_hm.playerStack += half;
        g_hm.aiStack     += g_hm.pot - half;
        snprintf(g_hm.lastAction, sizeof(g_hm.lastAction), "Split pot!");
    }
    g_hm.pot = 0;
}

// ── AI status message (cycled with timer) ──────────────────────────────────

const char* holdemAIStatus() {
    if (millis() - g_hm.statusTimer > 2000) {
        g_hm.statusIdx = (g_hm.statusIdx + 1) % HM_STATUS_COUNT;
        g_hm.statusTimer = millis();
    }
    return g_hmStatuses[g_hm.statusIdx];
}
