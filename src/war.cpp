/**
 * War — simplest card game: flip a card, highest wins. Ties = WAR!
 * Player vs dealer. 52-card deck split 26-26. First to all 52 cards wins.
 */

#include "war.h"
#include "cards.h"
#include "theme.h"

WarState g_war;

// ═══════════════════════════════════════════════════════════════════════════════
// Card comparison — Ace high (1 → 14)
// ═══════════════════════════════════════════════════════════════════════════════

static int warCardValue(uint8_t card) {
    uint8_t r = cardRank(card);
    if (r == 1) return 14;   // Ace high
    return r;
}

static int warCompare(uint8_t a, uint8_t b) {
    int va = warCardValue(a), vb = warCardValue(b);
    if (va > vb) return 1;
    if (vb > va) return -1;
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pile management
//   Player pile: deck[0 .. playerCount-1], top is at playerCount-1
//   Dealer pile: deck[26 .. 26+dealerCount-1], top is at 26+dealerCount-1
// ═══════════════════════════════════════════════════════════════════════════════

static int playerCount() { return g_war.playerEnd - g_war.playerStart; }
static int dealerCount() { return g_war.dealerEnd - g_war.dealerStart; }

// Draw top card from player pile
static uint8_t warPopPlayer() {
    int cnt = playerCount();
    if (cnt <= 0) return 0xFF;
    g_war.playerEnd--;
    return g_war.deck[g_war.playerEnd];
}

// Draw top card from dealer pile
static uint8_t warPopDealer() {
    int cnt = dealerCount();
    if (cnt <= 0) return 0xFF;
    g_war.dealerEnd--;
    return g_war.deck[g_war.dealerEnd];
}

// Add N cards to bottom of player pile (cards[] goes to the bottom in order)
static void warAddToPlayer(const uint8_t* cards, int n) {
    if (n <= 0) return;
    int cnt = playerCount();
    // Shift existing player cards right by n
    memmove(&g_war.deck[g_war.playerStart + n], &g_war.deck[g_war.playerStart], cnt);
    memcpy(&g_war.deck[g_war.playerStart], cards, n);
    g_war.playerEnd += n;
}

// Add N cards to bottom of dealer pile
static void warAddToDealer(const uint8_t* cards, int n) {
    if (n <= 0) return;
    int cnt = dealerCount();
    memmove(&g_war.deck[g_war.dealerStart + n], &g_war.deck[g_war.dealerStart], cnt);
    memcpy(&g_war.deck[g_war.dealerStart], cards, n);
    g_war.dealerEnd += n;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shuffle a range of the deck
// ═══════════════════════════════════════════════════════════════════════════════

static void warShuffleRange(int start, int end) {
    int n = end - start;
    for (int i = n - 1; i > 0; i--) {
        int j = random(i + 1);
        uint8_t t = g_war.deck[start + i];
        g_war.deck[start + i] = g_war.deck[start + j];
        g_war.deck[start + j] = t;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drawing
// ═══════════════════════════════════════════════════════════════════════════════

// Tiny card back for war burn cards
static void drawWarCardBack(TFT_eSPI &d, int x, int y) {
    d.fillRoundRect(x, y, WAR_WAR_CARD_W, WAR_WAR_CARD_H, 3, COL_BG);
    d.drawRoundRect(x, y, WAR_WAR_CARD_W, WAR_WAR_CARD_H, 3, g_themeColor);
    // Simple cross-hatch
    for (int cy = y + 3; cy < y + WAR_WAR_CARD_H - 3; cy += 5)
        for (int cx = x + 3; cx < x + WAR_WAR_CARD_W - 3; cx += 5)
            d.fillRect(cx, cy, 2, 2, (g_themeColor >> 1) & 0x7BEF);
}

// Tiny card face for war reveal card
static void drawWarCardFace(TFT_eSPI &d, int x, int y, uint8_t card) {
    uint8_t rank = cardRank(card);
    uint8_t suit = cardSuit(card);
    uint16_t col = g_themeColor;

    d.fillRoundRect(x, y, WAR_WAR_CARD_W, WAR_WAR_CARD_H, 3, COL_BG);
    d.drawRoundRect(x, y, WAR_WAR_CARD_W, WAR_WAR_CARD_H, 3, col);

    if (rank == 0) {
        d.setTextFont(1); d.setTextColor(col, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("J", x + WAR_WAR_CARD_W/2, y + WAR_WAR_CARD_H/2);
        return;
    }

    // Corner rank only — too small for suit symbol
    d.setTextFont(1); d.setTextColor(col, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString(rankStr(rank), x + WAR_WAR_CARD_W/2, y + WAR_WAR_CARD_H/2);
}

static void warDrawScreen(TFT_eSPI &d, unsigned long credits) {
    d.fillScreen(COL_BG);

    // ── Back to Video Poker (top-left) ──
    d.fillRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, COL_BG);
    d.drawRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, g_themeColor);
    d.drawRoundRect(HM_BACK_X + 1, HM_BACK_Y + 1, HM_BACK_W - 2, HM_BACK_H - 2, 4, g_themeColor);
    d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString("VIDEO POKER", HM_BACK_X + HM_BACK_W/2, HM_BACK_Y + HM_BACK_H/2);

    // ── Credits (centered on top bar, bigger font) ──
    d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(MC_DATUM);
    char cbuf[20];
    sprintf(cbuf, "CR: %lu", credits);
    d.drawString(cbuf, SCREEN_W / 2, 5);

    // ── Power button (matching main.cpp style) ──
    d.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R, g_themeColor);
    d.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R - 1, g_themeColor);
    d.drawLine(PWR_BTN_X,     PWR_BTN_Y - PWR_BTN_R + 3,
               PWR_BTN_X,     PWR_BTN_Y - 1, g_themeColor);
    d.drawLine(PWR_BTN_X - 1, PWR_BTN_Y - PWR_BTN_R + 3,
               PWR_BTN_X + 1, PWR_BTN_Y - PWR_BTN_R + 3, g_themeColor);

    // ── Pile count labels ──
    d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(TC_DATUM);
    char buf[20];
    sprintf(buf, "DEALER: %d", dealerCount());
    d.drawString(buf, WAR_DLR_CARD_X + CARD_W/2, WAR_LABEL_Y);
    sprintf(buf, "YOU: %d", playerCount());
    d.drawString(buf, WAR_PLY_CARD_X + CARD_W/2, WAR_LABEL_Y);

    if (g_war.phase == WAR_IDLE) {
        // Empty card areas — draw card back placeholders
        drawCardBack(d, WAR_DLR_CARD_X, WAR_DLR_CARD_Y);
        drawCardBack(d, WAR_PLY_CARD_X, WAR_PLY_CARD_Y);

        // Info
        d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("WAR", SCREEN_W/2, WAR_RESULT_Y);
        d.setTextFont(1);
        d.drawString("Win all 52 cards to collect 25 credits!", SCREEN_W/2, WAR_RESULT_Y + 20);

        // DEAL button
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        d.fillRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, COL_BG);
        d.drawRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, g_themeColor);
        d.drawRoundRect(bx + 1, WAR_BTN_Y + 1, WAR_BTN_W - 2, WAR_BTN_H - 2, 6, g_themeColor);
        d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("DEAL", SCREEN_W/2, WAR_BTN_Y + WAR_BTN_H/2);
        return;
    }

    if (g_war.phase == WAR_PLAYING) {
        // Both card backs — ready to flip
        drawCardBack(d, WAR_DLR_CARD_X, WAR_DLR_CARD_Y);
        drawCardBack(d, WAR_PLY_CARD_X, WAR_PLY_CARD_Y);

        // FLIP button
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        d.fillRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, COL_BG);
        d.drawRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, g_themeColor);
        d.drawRoundRect(bx + 1, WAR_BTN_Y + 1, WAR_BTN_W - 2, WAR_BTN_H - 2, 6, g_themeColor);
        d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("FLIP", SCREEN_W/2, WAR_BTN_Y + WAR_BTN_H/2);
        return;
    }

    // WAR_REVEAL / WAR_WAR / WAR_GAME_OVER — show face-up cards
    if (g_war.playerCard != 0xFF)
        drawCardFace(d, WAR_PLY_CARD_X, WAR_PLY_CARD_Y, g_war.playerCard);
    else
        drawCardBack(d, WAR_PLY_CARD_X, WAR_PLY_CARD_Y);

    if (g_war.dealerCard != 0xFF)
        drawCardFace(d, WAR_DLR_CARD_X, WAR_DLR_CARD_Y, g_war.dealerCard);
    else
        drawCardBack(d, WAR_DLR_CARD_X, WAR_DLR_CARD_Y);

    // War zone — dealer burn + face-up on LEFT, player burn + face-up on RIGHT
    if (g_war.inWar || g_war.phase == WAR_WAR) {
        // "WAR!" depth label
        d.setTextFont(1); d.setTextColor(COL_RED, COL_BG);
        d.setTextDatum(MC_DATUM);
        if (g_war.warDepth > 1) {
            char wbuf[20];
            sprintf(wbuf, "WAR x%d!", g_war.warDepth);
            d.drawString(wbuf, SCREEN_W/2, WAR_WAR_Y - 14);
        } else {
            d.drawString("WAR!", SCREEN_W/2, WAR_WAR_Y - 14);
        }

        // Each side: 3 face-down + 1 face-up = 4 cards total
        // Dealer's war cards: left of center
        int groupW = 4 * WAR_WAR_CARD_W + 3 * WAR_WAR_GAP;
        int dlrStartX = SCREEN_W/2 - groupW - 10;  // dealer group on left
        int plyStartX = SCREEN_W/2 + 10;            // player group on right

        // Dealer war cards
        for (int i = 0; i < 3; i++) {
            int cx = dlrStartX + i * (WAR_WAR_CARD_W + WAR_WAR_GAP);
            if (g_war.animStep > i)
                drawWarCardBack(d, cx, WAR_WAR_Y);
        }
        if (g_war.animStep >= 3 && g_war.warDealerCard != 0xFF)
            drawWarCardFace(d, dlrStartX + 3 * (WAR_WAR_CARD_W + WAR_WAR_GAP),
                           WAR_WAR_Y, g_war.warDealerCard);

        // Player war cards
        for (int i = 0; i < 3; i++) {
            int cx = plyStartX + i * (WAR_WAR_CARD_W + WAR_WAR_GAP);
            if (g_war.animStep > i)
                drawWarCardBack(d, cx, WAR_WAR_Y);
        }
        if (g_war.animStep >= 3 && g_war.warPlayerCard != 0xFF)
            drawWarCardFace(d, plyStartX + 3 * (WAR_WAR_CARD_W + WAR_WAR_GAP),
                           WAR_WAR_Y, g_war.warPlayerCard);
    }

    // Result text
    d.setTextFont(2); d.setTextColor(COL_GOLD, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString(g_war.resultMsg, SCREEN_W/2, WAR_RESULT_Y);

    // Action button (reveal / game over)
    if (g_war.phase == WAR_REVEAL) {
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        d.fillRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, COL_BG);
        d.drawRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, g_themeColor);
        d.drawRoundRect(bx + 1, WAR_BTN_Y + 1, WAR_BTN_W - 2, WAR_BTN_H - 2, 6, g_themeColor);
        d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("CONTINUE", SCREEN_W/2, WAR_BTN_Y + WAR_BTN_H/2);
    }

    if (g_war.phase == WAR_GAME_OVER) {
        d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("GAME  OVER", SCREEN_W/2, WAR_RESULT_Y + 24);

        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        d.fillRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, COL_BG);
        d.drawRoundRect(bx, WAR_BTN_Y, WAR_BTN_W, WAR_BTN_H, 6, g_themeColor);
        d.drawRoundRect(bx + 1, WAR_BTN_Y + 1, WAR_BTN_W - 2, WAR_BTN_H - 2, 6, g_themeColor);
        d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("NEW  GAME", SCREEN_W/2, WAR_BTN_Y + WAR_BTN_H/2);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Game logic
// ═══════════════════════════════════════════════════════════════════════════════

static void warStartGame() {
    // Initialize full deck
    for (int i = 0; i < 52; i++) g_war.deck[i] = i;
    // Shuffle
    warShuffleRange(0, 52);
    // Split
    g_war.playerStart = 0;
    g_war.playerEnd   = 26;
    g_war.dealerStart = 26;
    g_war.dealerEnd   = 52;
    // Shuffle each half independently
    warShuffleRange(g_war.playerStart, g_war.playerEnd);
    warShuffleRange(g_war.dealerStart, g_war.dealerEnd);

    g_war.playerCard  = 0xFF;
    g_war.dealerCard  = 0xFF;
    g_war.inWar       = false;
    g_war.warDepth    = 0;
    g_war.lastResult  = 0;
    g_war.roundsWon   = 0;
    g_war.roundsLost  = 0;
    g_war.payout      = 0;
    g_war.animTimer   = 0;
    g_war.animStep    = 0;
    g_war.warPotCount = 0;
    g_war.resultMsg[0] = '\0';

    g_war.phase = WAR_PLAYING;
}

static void warDoFlip() {
    int pc = playerCount(), dc = dealerCount();
    if (pc <= 0 || dc <= 0) {
        // Game over
        if (dc <= 0) {
            g_war.payout = WAR_PAYOUT;
            sprintf(g_war.resultMsg, "YOU  WIN!  +%lu", WAR_PAYOUT);
        } else {
            g_war.payout = 0;
            strcpy(g_war.resultMsg, "YOU  LOSE");
        }
        g_war.phase = WAR_GAME_OVER;
        return;
    }

    g_war.playerCard = warPopPlayer();
    g_war.dealerCard = warPopDealer();
    g_war.inWar = false;
    g_war.warDepth = 0;

    int cmp = warCompare(g_war.playerCard, g_war.dealerCard);

    if (cmp > 0) {
        // Player wins the round
        uint8_t won[2] = { g_war.playerCard, g_war.dealerCard };
        warAddToPlayer(won, 2);
        g_war.lastResult = 1;
        g_war.roundsWon++;
        strcpy(g_war.resultMsg, "YOU  WIN  THIS  ROUND!");
    } else if (cmp < 0) {
        // Dealer wins the round
        uint8_t won[2] = { g_war.dealerCard, g_war.playerCard };
        warAddToDealer(won, 2);
        g_war.lastResult = -1;
        g_war.roundsLost++;
        strcpy(g_war.resultMsg, "DEALER  WINS  ROUND");
    } else {
        // WAR!
        g_war.inWar = true;
        g_war.warDepth = 1;
        g_war.warPotCount = 0;
        g_war.lastResult = 0;
        sprintf(g_war.resultMsg, "WAR!");
        // Start war animation
        g_war.animTimer = millis();
        g_war.animStep = 0;
        g_war.phase = WAR_WAR;
        return;
    }

    // Periodic shuffle to break deterministic card cycles (every ~7 rounds)
    int totalRounds = g_war.roundsWon + g_war.roundsLost;
    if (totalRounds > 0 && (totalRounds % 7) == 0) {
        warShuffleRange(g_war.playerStart, g_war.playerEnd);
        warShuffleRange(g_war.dealerStart, g_war.dealerEnd);
    }

    g_war.phase = WAR_REVEAL;

    // Check for game over
    if (dc <= 0 || pc <= 0) {
        if (dealerCount() <= 0) {
            g_war.payout = WAR_PAYOUT;
            sprintf(g_war.resultMsg, "YOU  WIN!  +%lu", WAR_PAYOUT);
        } else if (playerCount() <= 0) {
            g_war.payout = 0;
            strcpy(g_war.resultMsg, "GAME  OVER  —  YOU  LOSE");
        }
        g_war.phase = WAR_GAME_OVER;
    }
}

static void warResolveWar() {
    int pc = playerCount(), dc = dealerCount();

    // ── Accumulate the two tied cards into war pot (from previous tie) ──
    // First war: playerCard/dealerCard are the tied cards
    // Subsequent wars: they were set from the previous war's face-up cards
    if (g_war.warDepth == 1) {
        // First war — add the tied cards to the pot
        g_war.warPot[g_war.warPotCount++] = g_war.playerCard;
        g_war.warPot[g_war.warPotCount++] = g_war.dealerCard;
    } else {
        // Subsequent war — the previous face-up cards (which were tied) go to pot
        g_war.warPot[g_war.warPotCount++] = g_war.playerCard;
        g_war.warPot[g_war.warPotCount++] = g_war.dealerCard;
    }

    // Need at least 4 cards each for war (3 burn + 1 face-up)
    if (pc < 4 || dc < 4) {
        // Short side loses — award pot + remaining cards to opponent
        if (pc < 4) {
            // Player can't fight — dealer wins everything
            warAddToDealer(g_war.warPot, g_war.warPotCount);
            while (playerCount() > 0) warAddToDealer(&g_war.deck[--g_war.playerEnd], 1);
            g_war.inWar = false;
            g_war.warPotCount = 0;
            g_war.payout = 0;
            strcpy(g_war.resultMsg, "NO CARDS — YOU LOSE");
            g_war.phase = WAR_GAME_OVER;
            return;
        } else {
            // Dealer can't fight — player wins everything
            warAddToPlayer(g_war.warPot, g_war.warPotCount);
            while (dealerCount() > 0) warAddToPlayer(&g_war.deck[--g_war.dealerEnd], 1);
            g_war.inWar = false;
            g_war.warPotCount = 0;
            g_war.payout = WAR_PAYOUT;
            sprintf(g_war.resultMsg, "YOU  WIN!  +%lu", WAR_PAYOUT);
            g_war.phase = WAR_GAME_OVER;
            return;
        }
    }

    // ── Burn 3 cards from each side → add to pot ──
    for (int i = 0; i < 3; i++) {
        g_war.warPlayerBurn[i] = warPopPlayer();
        g_war.warPot[g_war.warPotCount++] = g_war.warPlayerBurn[i];
    }
    for (int i = 0; i < 3; i++) {
        g_war.warDealerBurn[i] = warPopDealer();
        g_war.warPot[g_war.warPotCount++] = g_war.warDealerBurn[i];
    }

    // ── Face-up war cards for comparison ──
    g_war.warPlayerCard = warPopPlayer();
    g_war.warDealerCard = warPopDealer();

    int cmp = warCompare(g_war.warPlayerCard, g_war.warDealerCard);

    if (cmp > 0) {
        // ── Player wins the war ──
        g_war.warPot[g_war.warPotCount++] = g_war.warPlayerCard;
        g_war.warPot[g_war.warPotCount++] = g_war.warDealerCard;
        warAddToPlayer(g_war.warPot, g_war.warPotCount);
        warShuffleRange(g_war.playerStart, g_war.playerEnd);  // break deterministic cycles
        g_war.warPotCount = 0;
        g_war.inWar = false;
        g_war.lastResult = 1;
        g_war.roundsWon++;
        sprintf(g_war.resultMsg, "YOU  WIN  THE  WAR!");
        g_war.phase = WAR_REVEAL;
    } else if (cmp < 0) {
        // ── Dealer wins the war ──
        g_war.warPot[g_war.warPotCount++] = g_war.warPlayerCard;
        g_war.warPot[g_war.warPotCount++] = g_war.warDealerCard;
        warAddToDealer(g_war.warPot, g_war.warPotCount);
        warShuffleRange(g_war.dealerStart, g_war.dealerEnd);  // break deterministic cycles
        g_war.warPotCount = 0;
        g_war.inWar = false;
        g_war.lastResult = -1;
        g_war.roundsLost++;
        sprintf(g_war.resultMsg, "DEALER  WINS  WAR");
        g_war.phase = WAR_REVEAL;
    } else {
        // ── War ties again! ──
        g_war.warDepth++;
        // Current face-up cards become the new "tied" cards for next war
        // (already not in the pot — they'll be added on next warResolveWar entry)
        g_war.playerCard = g_war.warPlayerCard;
        g_war.dealerCard = g_war.warDealerCard;

        if (g_war.warDepth >= WAR_MAX_DEPTH) {
            // Split the pot (not including the current face-up cards)
            int half = g_war.warPotCount / 2;
            warAddToPlayer(g_war.warPot, half);
            warAddToDealer(g_war.warPot + half, g_war.warPotCount - half);
            // Return face-up cards to each side
            warAddToPlayer(&g_war.playerCard, 1);
            warAddToDealer(&g_war.dealerCard, 1);
            // Shuffle both piles to break deterministic tie cycles
            warShuffleRange(g_war.playerStart, g_war.playerEnd);
            warShuffleRange(g_war.dealerStart, g_war.dealerEnd);
            g_war.warPotCount = 0;
            g_war.inWar = false;
            g_war.lastResult = 0;
            sprintf(g_war.resultMsg, "TRIPLE  WAR  —  SPLIT!");
            g_war.phase = WAR_REVEAL;
        } else {
            // Another war — stay in WAR_WAR phase for animation
            g_war.animStep = 0;
            g_war.animTimer = millis();
            sprintf(g_war.resultMsg, "ANOTHER  WAR!");
        }
        return;
    }

    // Check game over after resolution
    if (playerCount() <= 0) {
        g_war.payout = 0;
        strcpy(g_war.resultMsg, "GAME  OVER  —  YOU  LOSE");
        g_war.phase = WAR_GAME_OVER;
    } else if (dealerCount() <= 0) {
        g_war.payout = WAR_PAYOUT;
        sprintf(g_war.resultMsg, "YOU  WIN!  +%lu", WAR_PAYOUT);
        g_war.phase = WAR_GAME_OVER;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════════

void warInit() {
    memset(&g_war, 0, sizeof(g_war));
    g_war.phase       = WAR_IDLE;
    g_war.playerCard  = 0xFF;
    g_war.dealerCard  = 0xFF;
    g_war.warPlayerCard = 0xFF;
    g_war.warDealerCard = 0xFF;
    g_war.warPotCount   = 0;
}

void warDraw(TFT_eSPI &d, unsigned long credits) {
    warDrawScreen(d, credits);
}

bool warTap(TFT_eSPI &d, int16_t tx, int16_t ty, unsigned long &credits) {
    // ── Power button (global) ──
    {
        int dx = tx - PWR_BTN_X, dy = ty - PWR_BTN_Y;
        if (dx*dx + dy*dy <= PWR_BTN_R * PWR_BTN_R + 4) {
            d.fillScreen(COL_BG);
            d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
            d.setTextDatum(MC_DATUM);
            d.drawString("SLEEP...", SCREEN_W/2, SCREEN_H/2);
            delay(400);
            esp_deep_sleep_start();
            return true;
        }
    }

    if (g_war.phase == WAR_IDLE) {
        // DEAL button
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        if (tx >= bx && tx <= bx + WAR_BTN_W &&
            ty >= WAR_BTN_Y && ty <= WAR_BTN_Y + WAR_BTN_H) {
            if (credits >= WAR_BET) {
                credits -= WAR_BET;
                warStartGame();
                return true;
            }
            return false;
        }
        return false;
    }

    if (g_war.phase == WAR_PLAYING) {
        // FLIP button
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        if (tx >= bx && tx <= bx + WAR_BTN_W &&
            ty >= WAR_BTN_Y && ty <= WAR_BTN_Y + WAR_BTN_H) {
            warDoFlip();
            return true;
        }
        return false;
    }

    if (g_war.phase == WAR_REVEAL) {
        // CONTINUE button (or tap anywhere to continue)
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        if (tx >= bx && tx <= bx + WAR_BTN_W &&
            ty >= WAR_BTN_Y && ty <= WAR_BTN_Y + WAR_BTN_H) {
            g_war.phase = WAR_PLAYING;
            g_war.playerCard = 0xFF;
            g_war.dealerCard = 0xFF;
            g_war.inWar = false;
            g_war.warDepth = 0;
            g_war.resultMsg[0] = '\0';
            return true;
        }
        return false;
    }

    if (g_war.phase == WAR_GAME_OVER) {
        // NEW GAME button
        int bx = SCREEN_W/2 - WAR_BTN_W/2;
        if (tx >= bx && tx <= bx + WAR_BTN_W &&
            ty >= WAR_BTN_Y && ty <= WAR_BTN_Y + WAR_BTN_H) {
            // Award payout, then reset
            credits += g_war.payout;
            g_war.payout = 0;
            g_war.phase = WAR_IDLE;
            g_war.playerCard = 0xFF;
            g_war.dealerCard = 0xFF;
            g_war.resultMsg[0] = '\0';
            return true;
        }
        return false;
    }

    return false;
}

bool warTick(TFT_eSPI &d, unsigned long credits) {
    if (g_war.phase != WAR_WAR) return false;
    if (!g_war.inWar) return false;

    unsigned long now = millis();
    if (g_war.animTimer == 0) {
        g_war.animTimer = now;
        g_war.animStep = 0;
        return false;
    }

    // Step through war animation: 3 burn cards (400ms each), then reveal (600ms)
    int delayMs = (g_war.animStep < 3) ? 400 : 600;

    if (now - g_war.animTimer >= delayMs) {
        g_war.animTimer = now;
        g_war.animStep++;

        if (g_war.animStep >= 4) {
            // All cards revealed — resolve the war
            warResolveWar();
        }
        return true;  // redraw to show animation progress
    }
    return false;
}
