/**
 * Blackjack — player vs dealer, closest to 21 wins
 * Hit/Stand/Double Down/Split. Standard casino rules.
 */

#include "blackjack.h"
#include "cards.h"
#include "theme.h"

BlackjackState g_bj;

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

// ── Card encoding helpers (matching cards.h: card = rank*4 + suit) ────────

static uint8_t makeCard(uint8_t rank, uint8_t suit) { return rank * 4 + suit; }

// ── Hand value with soft/hard Ace ────────────────────────────────────────

static int bjHandValue(const uint8_t* cards, uint8_t count, bool* soft = nullptr) {
    int total = 0;
    int aces = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t rank = cardRank(cards[i]);
        if (rank == 0) continue;   // joker guard
        if (rank == 1) { total += 11; aces++; }
        else if (rank > 10) total += 10;
        else total += rank;
    }
    while (total > 21 && aces > 0) { total -= 10; aces--; }
    if (soft) *soft = (aces > 0);
    return total;
}

// ── Natural blackjack check ───────────────────────────────────────────────

static bool bjIsBlackjack(const uint8_t* cards, uint8_t count) {
    if (count != 2) return false;
    uint8_t r0 = cardRank(cards[0]), r1 = cardRank(cards[1]);
    return ((r0 == 1 && r1 >= 10) || (r1 == 1 && r0 >= 10));
}

// ── Fisher-Yates shuffle ──────────────────────────────────────────────────

static void bjShuffle(uint8_t* deck) {
    // Use cards 4-55: ranks 1-13 (A-K), suits 0-3 — 52 cards, no Jokers
    for (int i = 0; i < 52; i++) deck[i] = i + 4;
    for (int i = 51; i > 0; i--) {
        int j = random(i + 1);
        uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
}

// ── Draw next card from deck ──────────────────────────────────────────────

static uint8_t bjDrawCard() {
    if (g_bj.deckPos >= 52) { bjShuffle(g_bj.deck); g_bj.deckPos = 0; }
    return g_bj.deck[g_bj.deckPos++];
}

// ── Which hand value to show (dealer: only visible cards before turn) ─────

static int dealerVisibleValue() {
    // Only first card is face-up until dealer turn
    uint8_t visible = g_bj.dealer.count;
    if (g_bj.dealerRevealed < g_bj.dealer.count) visible = g_bj.dealerRevealed;
    // If only showing 1 of 2, show value of that one card
    if (visible == 1 && g_bj.dealer.count >= 2) {
        uint8_t rank = cardRank(g_bj.dealer.cards[0]);
        if (rank == 1) return 11;  // Ace showing as 11
        if (rank > 10) return 10;
        return rank;
    }
    // Otherwise show full value of revealed cards
    uint8_t tmp[7]; memcpy(tmp, g_bj.dealer.cards, visible);
    return bjHandValue(tmp, visible);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drawing — small cards (40×52, matching Hold'em hole card style)
// ═══════════════════════════════════════════════════════════════════════════════

static void drawBjCardFace(TFT_eSPI &d, int x, int y, uint8_t card) {
    uint8_t rank = cardRank(card);
    uint8_t suit = cardSuit(card);
    uint16_t col = g_themeColor;

    d.fillRoundRect(x, y, BJ_CARD_W, BJ_CARD_H, 4, COL_BG);
    d.drawRoundRect(x, y, BJ_CARD_W, BJ_CARD_H, 4, col);

    if (rank == 0) {
        d.setTextFont(1); d.setTextColor(col, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("J", x + BJ_CARD_W/2, y + BJ_CARD_H/2);
        return;
    }

    d.setTextFont(1); d.setTextColor(col, COL_BG);
    d.setTextDatum(TL_DATUM);
    d.drawString(rankStr(rank), x + 3, y + 2);
    d.setTextDatum(BR_DATUM);
    d.drawString(rankStr(rank), x + BJ_CARD_W - 3, y + BJ_CARD_H - 2);
    // Center suit
    drawSuitSymbol(d, x + BJ_CARD_W/2, y + BJ_CARD_H/2 + 2, 24, suit);
}

static void drawBjCardBack(TFT_eSPI &d, int x, int y) {
    d.fillRoundRect(x, y, BJ_CARD_W, BJ_CARD_H, 4, COL_BG);
    d.drawRoundRect(x, y, BJ_CARD_W, BJ_CARD_H, 4, g_themeColor);
    for (int cy = y + 4; cy < y + BJ_CARD_H - 4; cy += 6)
        for (int cx = x + 4; cx < x + BJ_CARD_W - 4; cx += 6)
            d.fillRect(cx, cy, 2, 2, (g_themeColor >> 1) & 0x7BEF);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Drawing — screen sections
// ═══════════════════════════════════════════════════════════════════════════════

static void drawBjDealerArea(TFT_eSPI &d) {
    // Dealer label — centered on top bar
    char buf[20];
    int val = dealerVisibleValue();
    if (g_bj.phase == BJ_HAND_OVER) val = bjHandValue(g_bj.dealer.cards, g_bj.dealer.count);
    sprintf(buf, "DEALER: %d", val);

    d.setTextFont(2); d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString(buf, SCREEN_W / 2, 5);

    // Cards — centered in area left of buttons (shifted right)
    int totalW = g_bj.dealer.count * BJ_CARD_GAP;
    int startX = (BJ_BTN_X - totalW) / 2 + 41;
    if (startX < 4) startX = 4;

    for (int i = 0; i < g_bj.dealer.count; i++) {
        int cx = startX + i * BJ_CARD_GAP;
        bool faceUp = (i < (int)g_bj.dealerRevealed) || (g_bj.phase == BJ_HAND_OVER);
        if (faceUp) drawBjCardFace(d, cx, BJ_DEALER_Y, g_bj.dealer.cards[i]);
        else        drawBjCardBack(d, cx, BJ_DEALER_Y);
    }
}

static void drawBjPlayerHand(TFT_eSPI &d, int handIdx, int y, bool isActive) {
    BjHand &h = g_bj.hands[handIdx];
    if (h.count == 0) return;

    // Center cards in area left of buttons (shifted right, same as dealer)
    int totalW = h.count * BJ_CARD_GAP;
    int startX = (BJ_BTN_X - totalW) / 2 + 41;
    if (startX < 4) startX = 4;

    for (int i = 0; i < h.count; i++) {
        int cx = startX + i * BJ_CARD_GAP;
        drawBjCardFace(d, cx, y, h.cards[i]);
    }
}

static void drawBjInfo(TFT_eSPI &d) {
    d.fillRect(55, BJ_INFO_Y, BJ_BTN_X - 55 - 4, BJ_INFO_H, COL_BG);
    d.setTextFont(2); d.setTextDatum(MC_DATUM);

    if (g_bj.phase == BJ_IDLE) {
        d.setTextColor(g_themeColor, COL_BG);
        d.drawString("PRESS  TO  DEAL", SCREEN_W / 2, BJ_INFO_Y + 18);
        return;
    }

    if (g_bj.phase == BJ_PLAYING) {
        BjHand &h = g_bj.hands[g_bj.activeHand];
        int val = bjHandValue(h.cards, h.count);
        bool soft; bjHandValue(h.cards, h.count, &soft);

        d.setTextColor(g_themeColor, COL_BG);
        char buf[32];
        if (soft && val <= 21)
            sprintf(buf, "HAND: %d / %d", val - 10, val);  // soft/hard
        else
            sprintf(buf, "HAND: %d", val);

        if (g_bj.handCount == 2) {
            char hbuf[40];
            sprintf(hbuf, "%s  (HAND %d)", buf, g_bj.activeHand + 1);
            d.drawString(hbuf, SCREEN_W / 2, BJ_INFO_Y + 13);
        } else {
            d.drawString(buf, SCREEN_W / 2, BJ_INFO_Y + 13);
        }

        d.setTextFont(1);
        d.drawString("HIT  STAND  DOUBLE  SPLIT", SCREEN_W / 2, BJ_INFO_Y + 46);
        return;
    }

    if (g_bj.phase == BJ_DEALER_TURN) {
        d.setTextColor(g_themeColor, COL_BG);
        d.setTextFont(2);
        d.drawString("DEALER  PLAYING...", SCREEN_W / 2, BJ_INFO_Y + 18);
        return;
    }

    if (g_bj.phase == BJ_HAND_OVER) {
        d.setTextColor(COL_GOLD, COL_BG);
        d.setTextFont(2);
        d.drawString(g_bj.resultMsg, SCREEN_W / 2, BJ_INFO_Y + 8);

        d.setTextFont(1);
        d.setTextColor(g_themeColor, COL_BG);
        d.drawString("TAP  NEXT  HAND  TO  CONTINUE", SCREEN_W / 2, BJ_INFO_Y + 41);
    }
}

static void drawBjButton(TFT_eSPI &d, int y, const char* label, bool enabled) {
    uint16_t col = enabled ? g_themeColor : COL_MID_GRAY;
    d.fillRoundRect(BJ_BTN_X, y, BJ_BTN_W, BJ_BTN_H, 5, COL_BG);
    d.drawRoundRect(BJ_BTN_X, y, BJ_BTN_W, BJ_BTN_H, 5, col);
    d.drawRoundRect(BJ_BTN_X + 1, y + 1, BJ_BTN_W - 2, BJ_BTN_H - 2, 5, col);
    d.setTextFont(1);
    d.setTextColor(col, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString(label, BJ_BTN_X + BJ_BTN_W/2, y + BJ_BTN_H/2);
}

static void drawBjButtons(TFT_eSPI &d) {
    if (g_bj.phase == BJ_IDLE) {
        // Bet cycle button (cycles 5→10→20→30→50→5)
        char buf[14];
        sprintf(buf, "BET %lu", g_bj.bet);
        drawBjButton(d, BJ_BET_BTN_Y, buf, true);
        // DEAL button
        drawBjButton(d, BJ_DEAL_BTN_Y, "DEAL", true);
        return;
    }

    if (g_bj.phase == BJ_PLAYING) {
        BjHand &h = g_bj.hands[g_bj.activeHand];
        int val = bjHandValue(h.cards, h.count);
        bool canDouble = (h.count == 2);
        bool canSplit  = (h.count == 2 && cardRank(h.cards[0]) == cardRank(h.cards[1])
                          && g_bj.handCount == 1);

        drawBjButton(d, BJ_HIT_Y,    "HIT",    !h.stood && !h.bust && val < 21);
        drawBjButton(d, BJ_STAND_Y,  "STAND",  !h.stood && !h.bust);
        drawBjButton(d, BJ_DOUBLE_Y, "DOUBLE", canDouble && !h.stood && !h.bust);
        drawBjButton(d, BJ_SPLIT_Y_BTN, "SPLIT", canSplit);
        return;
    }

    if (g_bj.phase == BJ_HAND_OVER) {
        drawBjButton(d, BJ_DEAL_BTN_Y, "NEXT", true);
        return;
    }
}

static void drawBjBetDisplay(TFT_eSPI &d) {
    char buf[16];
    if (g_bj.handCount == 2)
        sprintf(buf, "BET: %lu x2", g_bj.bet);
    else
        sprintf(buf, "BET: %lu", g_bj.bet);

    d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(TL_DATUM);
    d.drawString(buf, 6, BJ_BET_DISP_Y);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Game logic — forward declarations
// ═══════════════════════════════════════════════════════════════════════════════

static void bjPlayerStand();

// ═══════════════════════════════════════════════════════════════════════════════
// Game logic
// ═══════════════════════════════════════════════════════════════════════════════

static void bjDealHand() {
    // Reset hands
    memset(&g_bj.dealer, 0, sizeof(g_bj.dealer));
    memset(g_bj.hands, 0, sizeof(g_bj.hands));
    g_bj.handCount   = 1;
    g_bj.activeHand  = 0;
    g_bj.dealerRevealed = 1;        // only first card face-up
    g_bj.dealerDone  = false;
    g_bj.animTimer   = 0;
    g_bj.animStep    = 0;
    g_bj.resultMsg[0] = '\0';
    g_bj.payoutMain  = 0;
    g_bj.payoutSplit = 0;

    // Deal: player, dealer, player, dealer
    g_bj.hands[0].cards[g_bj.hands[0].count++] = bjDrawCard();
    g_bj.dealer.cards[g_bj.dealer.count++]       = bjDrawCard();
    g_bj.hands[0].cards[g_bj.hands[0].count++]  = bjDrawCard();
    g_bj.dealer.cards[g_bj.dealer.count++]       = bjDrawCard();

    // Check for naturals
    g_bj.hands[0].isBlackjack = bjIsBlackjack(g_bj.hands[0].cards, g_bj.hands[0].count);
    g_bj.dealer.isBlackjack   = bjIsBlackjack(g_bj.dealer.cards, g_bj.dealer.count);

    if (g_bj.hands[0].isBlackjack || g_bj.dealer.isBlackjack) {
        // Skip player turn — go straight to dealer reveal
        g_bj.hands[0].stood = true;
        g_bj.phase = BJ_DEALER_TURN;
        g_bj.dealerRevealed = g_bj.dealer.count;   // reveal all
        return;
    }

    // Check for 21 (not natural) — auto-stand
    if (bjHandValue(g_bj.hands[0].cards, g_bj.hands[0].count) >= 21) {
        g_bj.hands[0].stood = true;
        g_bj.phase = BJ_DEALER_TURN;
        return;
    }

    g_bj.phase = BJ_PLAYING;
}

static void bjPlayerHit() {
    BjHand &h = g_bj.hands[g_bj.activeHand];
    if (h.stood || h.bust) return;

    h.cards[h.count++] = bjDrawCard();
    int val = bjHandValue(h.cards, h.count);

    if (val > 21) {
        h.bust = true;
        h.stood = true;

        // If split and this hand busts, move to next hand or dealer
        if (g_bj.handCount == 2) {
            if (g_bj.activeHand == 0) {
                g_bj.activeHand = 1;
                // Deal 1 card to split hand if not yet dealt
                if (g_bj.hands[1].count == 1) {
                    g_bj.hands[1].cards[g_bj.hands[1].count++] = bjDrawCard();
                    // Check split Aces — auto stand after 1 card
                    if (cardRank(g_bj.hands[1].cards[0]) == 1) {
                        g_bj.hands[1].stood = true;
                        g_bj.phase = BJ_DEALER_TURN;
                        return;
                    }
                }
                // If split hand already has 2+ cards, just switch to it
                if (g_bj.hands[1].stood) {
                    g_bj.phase = BJ_DEALER_TURN;
                }
            } else {
                g_bj.phase = BJ_DEALER_TURN;
            }
        } else {
            g_bj.phase = BJ_DEALER_TURN;
        }
        return;
    }

    if (val == 21) {
        h.stood = true;
        bjPlayerStand();  // use stand logic to advance
        return;
    }

    // Keep playing
}

static void bjPlayerStand() {
    BjHand &h = g_bj.hands[g_bj.activeHand];
    h.stood = true;

    // If split and on first hand, switch to second hand
    if (g_bj.handCount == 2 && g_bj.activeHand == 0) {
        g_bj.activeHand = 1;
        // Deal 1 card to split hand if not yet dealt
        if (g_bj.hands[1].count == 1) {
            g_bj.hands[1].cards[g_bj.hands[1].count++] = bjDrawCard();
            // Check split Aces — auto stand after 1 card
            if (cardRank(g_bj.hands[1].cards[0]) == 1) {
                g_bj.hands[1].stood = true;
                g_bj.phase = BJ_DEALER_TURN;
                return;
            }
        }
        // Check for 21 on split hand
        if (bjHandValue(g_bj.hands[1].cards, g_bj.hands[1].count) >= 21) {
            g_bj.hands[1].stood = true;
            g_bj.phase = BJ_DEALER_TURN;
        }
        return;
    }

    // Both hands done (or no split) — dealer's turn
    g_bj.phase = BJ_DEALER_TURN;
}

static void bjPlayerDouble() {
    BjHand &h = g_bj.hands[g_bj.activeHand];
    if (h.count != 2 || h.stood) return;

    // Draw exactly 1 card
    h.cards[h.count++] = bjDrawCard();
    int val = bjHandValue(h.cards, h.count);
    if (val > 21) h.bust = true;
    h.stood = true;

    // If split, handle transition
    if (g_bj.handCount == 2 && g_bj.activeHand == 0) {
        g_bj.activeHand = 1;
        if (g_bj.hands[1].count == 1) {
            g_bj.hands[1].cards[g_bj.hands[1].count++] = bjDrawCard();
            if (cardRank(g_bj.hands[1].cards[0]) == 1) {
                g_bj.hands[1].stood = true;
            }
        }
        if (g_bj.hands[1].stood || g_bj.hands[1].count >= 2) {
            g_bj.phase = BJ_DEALER_TURN;
        }
        return;
    }

    g_bj.phase = BJ_DEALER_TURN;
}

static void bjPlayerSplit() {
    BjHand &h = g_bj.hands[0];
    if (h.count != 2 || cardRank(h.cards[0]) != cardRank(h.cards[1])
        || g_bj.handCount != 1) return;

    // Move second card to split hand
    g_bj.hands[1].cards[0] = h.cards[1];
    g_bj.hands[1].count = 1;
    g_bj.hands[1].stood = false;
    g_bj.hands[1].bust = false;
    g_bj.hands[1].isBlackjack = false;

    // Main hand keeps first card + gets a new one
    h.cards[1] = bjDrawCard();
    h.count = 2;
    h.stood = false;
    h.bust = false;
    h.isBlackjack = false;

    g_bj.handCount = 2;
    g_bj.activeHand = 0;

    // Split Aces: auto stand on main hand too after 1 card (first card stays, second is new)
    // Actually for Aces: each Ace hand gets exactly 1 card total
    if (cardRank(h.cards[0]) == 1) {
        h.stood = true;
        // Deal 1 card to each Ace hand
        h.cards[1] = bjDrawCard();  // replace what we had
        g_bj.hands[1].cards[1] = bjDrawCard();
        g_bj.hands[1].count = 2;
        g_bj.hands[1].stood = true;
        g_bj.phase = BJ_DEALER_TURN;
    }
}

static void bjCalculatePayouts() {
    int dealerVal = bjHandValue(g_bj.dealer.cards, g_bj.dealer.count);
    bool dealerBJ = g_bj.dealer.isBlackjack;
    bool dealerBust = (dealerVal > 21);

    g_bj.payoutMain  = 0;
    g_bj.payoutSplit = 0;

    for (int hi = 0; hi < g_bj.handCount; hi++) {
        BjHand &h = g_bj.hands[hi];
        if (h.count == 0) continue;

        unsigned long *payout = (hi == 0) ? &g_bj.payoutMain : &g_bj.payoutSplit;
        int playerVal = bjHandValue(h.cards, h.count);

        if (h.bust) {
            *payout = 0;
        } else if (h.isBlackjack && !dealerBJ) {
            *payout = g_bj.bet * 3 / 2;  // 3:2
        } else if (h.isBlackjack && dealerBJ) {
            *payout = g_bj.bet;           // push: both have BJ
        } else if (dealerBust) {
            *payout = g_bj.bet * 2;
        } else if (playerVal > dealerVal) {
            *payout = g_bj.bet * 2;
        } else if (playerVal == dealerVal) {
            *payout = g_bj.bet;           // push
        } else {
            *payout = 0;
        }
    }

    // Build result message
    if (g_bj.handCount == 2) {
        unsigned long total = g_bj.payoutMain + g_bj.payoutSplit;
        if (total > 0)
            sprintf(g_bj.resultMsg, "WIN  %lu", total);
        else if (g_bj.payoutMain == g_bj.bet || g_bj.payoutSplit == g_bj.bet)
            sprintf(g_bj.resultMsg, "PUSH  %lu", g_bj.payoutMain + g_bj.payoutSplit);
        else
            strcpy(g_bj.resultMsg, "DEALER  WINS");
    } else {
        if (g_bj.payoutMain > g_bj.bet * 2)
            sprintf(g_bj.resultMsg, "BLACKJACK!  +%lu", g_bj.payoutMain);
        else if (g_bj.payoutMain > g_bj.bet)
            sprintf(g_bj.resultMsg, "YOU  WIN  +%lu", g_bj.payoutMain);
        else if (g_bj.payoutMain == g_bj.bet)
            sprintf(g_bj.resultMsg, "PUSH  —  BET  RETURNED");
        else
            strcpy(g_bj.resultMsg, "DEALER  WINS");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════════

void blackjackInit() {
    memset(&g_bj, 0, sizeof(g_bj));
    g_bj.phase    = BJ_IDLE;
    g_bj.bet      = BJ_MIN_BET;
    g_bj.handCount = 1;
    bjShuffle(g_bj.deck);
    g_bj.deckPos = 0;
}

void blackjackDraw(TFT_eSPI &d, unsigned long credits) {
    d.fillScreen(COL_BG);

    // ── Back to Video Poker button (top-left) ──
    {
        d.fillRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, COL_BG);
        d.drawRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, g_themeColor);
        d.drawRoundRect(HM_BACK_X + 1, HM_BACK_Y + 1, HM_BACK_W - 2, HM_BACK_H - 2, 4, g_themeColor);
        d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(MC_DATUM);
        d.drawString("VIDEO POKER", HM_BACK_X + HM_BACK_W / 2, HM_BACK_Y + HM_BACK_H / 2);
    }

    // ── Credits + Bet (left side, under DEALER label) ──
    {
        d.setTextFont(1); d.setTextColor(g_themeColor, COL_BG);
        d.setTextDatum(TL_DATUM);
        char buf[20];
        sprintf(buf, "CR: %lu", credits);
        d.drawString(buf, 6, BJ_CREDITS_Y);
    }

    drawBjBetDisplay(d);

    // ── Dealer cards ──
    drawBjDealerArea(d);

    // ── Info area ──
    drawBjInfo(d);

    // ── Player cards ──
    drawBjPlayerHand(d, 0, BJ_PLAYER_Y,
                     (g_bj.activeHand == 0) && (g_bj.phase == BJ_PLAYING));
    if (g_bj.handCount == 2) {
        drawBjPlayerHand(d, 1, BJ_SPLIT_Y,
                         (g_bj.activeHand == 1) && (g_bj.phase == BJ_PLAYING));
    }

    // ── Right panel buttons ──
    drawBjButtons(d);

    // ── Power button (matching main.cpp style) ──
    d.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R, g_themeColor);
    d.drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R - 1, g_themeColor);
    d.drawLine(PWR_BTN_X,     PWR_BTN_Y - PWR_BTN_R + 3,
               PWR_BTN_X,     PWR_BTN_Y - 1, g_themeColor);
    d.drawLine(PWR_BTN_X - 1, PWR_BTN_Y - PWR_BTN_R + 3,
               PWR_BTN_X + 1, PWR_BTN_Y - PWR_BTN_R + 3, g_themeColor);
}

bool blackjackTap(TFT_eSPI &d, int16_t tx, int16_t ty, unsigned long &credits) {
    // ── Power button (global, top-right) ──
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

    if (g_bj.phase == BJ_IDLE) {
        // Bet cycle button (cycles 5→10→20→30→50→5)
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_BET_BTN_Y && ty <= BJ_BET_BTN_Y + BJ_BTN_H) {
            // Cycle bet: 5→10→20→30→50→5
            if (g_bj.bet >= 50) g_bj.bet = BJ_MIN_BET;
            else if (g_bj.bet >= 30) g_bj.bet = 50;
            else if (g_bj.bet >= 20) g_bj.bet = 30;
            else if (g_bj.bet >= 10) g_bj.bet = 20;
            else g_bj.bet = 10;
            if (g_bj.bet > credits) g_bj.bet = BJ_MIN_BET;  // clamp to affordable
            return true;
        }

        // DEAL button
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_DEAL_BTN_Y && ty <= BJ_DEAL_BTN_Y + BJ_BTN_H) {
            if (credits >= g_bj.bet) {
                credits -= g_bj.bet;
                bjDealHand();
                return true;
            }
            // Not enough credits — set bet to all they have and retry
            if (credits >= BJ_MIN_BET) {
                g_bj.bet = credits;
                credits -= g_bj.bet;
                bjDealHand();
                return true;
            }
            // Completely out — show message
            strcpy(g_bj.resultMsg, "OUT  OF  CREDITS");
            g_bj.phase = BJ_HAND_OVER;
            return true;
        }

        return false;
    }

    if (g_bj.phase == BJ_PLAYING) {
        // HIT
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_HIT_Y && ty <= BJ_HIT_Y + BJ_BTN_H) {
            BjHand &h = g_bj.hands[g_bj.activeHand];
            if (!h.stood && !h.bust && bjHandValue(h.cards, h.count) < 21) {
                bjPlayerHit();
                return true;
            }
        }

        // STAND
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_STAND_Y && ty <= BJ_STAND_Y + BJ_BTN_H) {
            BjHand &h = g_bj.hands[g_bj.activeHand];
            if (!h.stood && !h.bust) {
                bjPlayerStand();
                return true;
            }
        }

        // DOUBLE
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_DOUBLE_Y && ty <= BJ_DOUBLE_Y + BJ_BTN_H) {
            BjHand &h = g_bj.hands[g_bj.activeHand];
            if (h.count == 2 && !h.stood && !h.bust && credits >= g_bj.bet) {
                credits -= g_bj.bet;
                bjPlayerDouble();
                return true;
            }
        }

        // SPLIT
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_SPLIT_Y_BTN && ty <= BJ_SPLIT_Y_BTN + BJ_BTN_H) {
            BjHand &h = g_bj.hands[0];
            if (h.count == 2 && cardRank(h.cards[0]) == cardRank(h.cards[1])
                && g_bj.handCount == 1 && credits >= g_bj.bet) {
                credits -= g_bj.bet;
                bjPlayerSplit();
                return true;
            }
        }

        return false;
    }

    if (g_bj.phase == BJ_HAND_OVER) {
        // NEXT HAND button
        if (tx >= BJ_BTN_X && tx <= BJ_BTN_X + BJ_BTN_W &&
            ty >= BJ_DEAL_BTN_Y && ty <= BJ_DEAL_BTN_Y + BJ_BTN_H) {
            // Award payouts
            credits += g_bj.payoutMain + g_bj.payoutSplit;
            // Reset for next hand
            g_bj.phase = BJ_IDLE;
            g_bj.handCount = 1;
            g_bj.activeHand = 0;
            g_bj.resultMsg[0] = '\0';
            return true;
        }
        return false;
    }

    return false;
}

bool blackjackTick(TFT_eSPI &d, unsigned long credits) {
    if (g_bj.phase != BJ_DEALER_TURN) return false;

    unsigned long now = millis();

    // Init dealer turn on first tick — reveal hole card
    if (g_bj.animTimer == 0) {
        g_bj.animTimer = now;
        g_bj.animStep = 0;
        g_bj.dealerRevealed = 2;  // reveal hole card
        g_bj.dealerDone = false;
        return true;  // redraw to show hole card
    }

    // Wait 600ms between dealer actions
    if (now - g_bj.animTimer < 600) return false;
    g_bj.animTimer = now;

    int dealerVal = bjHandValue(g_bj.dealer.cards, g_bj.dealer.count);
    bool soft;
    bjHandValue(g_bj.dealer.cards, g_bj.dealer.count, &soft);

    // Dealer stands on all 17s (hard and soft)
    if (dealerVal >= 17) {
        g_bj.dealerDone = true;
        bjCalculatePayouts();
        g_bj.phase = BJ_HAND_OVER;
        return true;
    }

    // Dealer must hit on 16 or less
    if (dealerVal <= 16) {
        g_bj.dealer.cards[g_bj.dealer.count++] = bjDrawCard();
        g_bj.dealerRevealed = g_bj.dealer.count;
        g_bj.animStep++;

        // Re-evaluate after drawing
        dealerVal = bjHandValue(g_bj.dealer.cards, g_bj.dealer.count);
        if (dealerVal >= 17 || dealerVal > 21) {
            // Will stand on next tick (600ms from now)
        }

        // Safety: max dealer cards
        if (g_bj.dealer.count >= BJ_MAX_HAND) {
            g_bj.dealerDone = true;
            bjCalculatePayouts();
            g_bj.phase = BJ_HAND_OVER;
        }
        return true;  // redraw to show new dealer card
    }

    return false;  // shouldn't reach here
}
