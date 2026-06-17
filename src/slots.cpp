#include "slots.h"
#include "cards.h"
#include "theme.h"
#include <cstdio>
#include <cstring>

// ── Reel strip (16 stops, weighted) ───────────────────────────────────────
// ♣×4, ♦×3, ♥×3, ♠×2, 7×2, ★×1
static const uint8_t REEL_STRIP[SLOT_REEL_LENGTH] = {
    SYM_CLUB, SYM_DIAMOND, SYM_HEART, SYM_SPADE,
    SYM_CLUB, SYM_SEVEN,  SYM_DIAMOND, SYM_HEART,
    SYM_CLUB, SYM_SPADE,  SYM_DIAMOND, SYM_CLUB,
    SYM_HEART, SYM_SEVEN, SYM_WILD,    SYM_CLUB
};

// ── Bet values ─────────────────────────────────────────────────────────────
const uint8_t SLOT_BETS[SLOT_BET_COUNT] = { 1, 3, 5, 10 };

// ── Payout table ───────────────────────────────────────────────────────────
// Indexed by win type (see evaluateWin)
struct PayoutEntry {
    uint16_t multiplier;   // × bet
    const char *label;
};
static const PayoutEntry SLOT_PAYOUTS[] = {
    { 100, "★★★ JACKPOT!" },
    {  50, "777 LUCKY!" },
    {  25, "♠♠♠ SPADES!" },
    {  20, "♥♥♥ HEARTS!" },
    {  15, "♦♦♦ DIAMONDS!" },
    {  10, "♣♣♣ CLUBS!" },
    {   8, "★★ 2 WILDS" },
    {   5, "77 2 SEVENS" },
    {   4, "★+PAIR WILD" },
    {   3, "PAIR+WILD" },
    {   2, "ANY PAIR" },
    {   0, nullptr }
};

// ── Global state ────────────────────────────────────────────────────────────
SlotsState g_slots;

// ── Helpers ────────────────────────────────────────────────────────────────

static uint16_t symColor(uint8_t sym) {
    switch (sym) {
        case SYM_WILD:  return COL_GOLD;
        case SYM_SEVEN: return COL_RED;
        default:        return g_themeColor;
    }
}

// Draw one symbol inside a reel cell
static void drawReelSymbol(TFT_eSPI &tft, int x, int y, int w, int h, uint8_t sym) {
    uint16_t col = symColor(sym);
    uint16_t bg  = COL_BG;

    // Cell background + single border
    tft.fillRect(x, y, w, h, bg);
    tft.drawRoundRect(x, y, w, h, 4, col);

    int cx = x + w / 2;
    int cy = y + h / 2;

    if (sym == SYM_WILD) {
        drawStar(tft, cx, cy, 18, col);
    } else if (sym == SYM_SEVEN) {
        tft.setTextFont(4);
        tft.setTextColor(col, bg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("7", cx, cy);
    } else {
        drawSuitSymbol(tft, cx, cy + 2, 16, sym);
    }
}

// Draw one full reel column (3 visible symbols)
static void drawReel(TFT_eSPI &tft, int reelIdx, int baseX) {
    ReelState &r = g_slots.reels[reelIdx];
    int by = SLOT_REEL_Y;

    for (int row = 0; row < SLOT_VISIBLE; row++) {
        int sy = by + row * SLOT_SYM_H;
        // Last row may be partial (100 / 33 leaves 1px, first row gets extra)
        int sh = (row == 0) ? SLOT_SYM_H + 1 : SLOT_SYM_H;
        if (row == 2) sh = SLOT_REEL_H - 2 * SLOT_SYM_H - 1;  // remainder
        int idx = (r.pos + row) % SLOT_REEL_LENGTH;
        uint8_t sym = REEL_STRIP[idx];
        drawReelSymbol(tft, baseX, sy, SLOT_REEL_W, SLOT_SYM_H, sym);
    }
}

// Payline indicator removed — just draw nothing extra between reels

// Draw the mini paytable below reels — uses graphic suit symbols, not text
static void drawMiniPaytable(TFT_eSPI &tft) {
    // 6 pay entries: each is [symbol×3, multiplier] in a 3×2 grid
    // Left=low, middle=mid, right=high  (top row, then bottom row)
    struct { uint8_t sym; uint16_t mult; } entries[] = {
        { SYM_DIAMOND, 15 },   // left top
        { SYM_SPADE,   25 },   // middle top
        { SYM_WILD,   100 },   // right top
        { SYM_CLUB,    10 },   // left bottom
        { SYM_HEART,   20 },   // middle bottom
        { SYM_SEVEN,   50 },   // right bottom
    };

    int ew = 86, eh = 16;   // entry box size
    int gap = 6;
    int cols = 3;
    int totalW = cols * ew + (cols - 1) * gap;  // 3*86 + 2*6 = 270
    int x0 = (SCREEN_W - totalW) / 2;           // centered
    int rows = 2;

    for (int i = 0; i < 6; i++) {
        int col = i % cols;
        int row = i / cols;
        int ex = x0 + col * (ew + gap);
        int ey = SLOT_PAY_Y + row * (eh + 4);

        // Draw entry background
        tft.fillRoundRect(ex, ey, ew, eh, 3, COL_BG);
        tft.drawRoundRect(ex, ey, ew, eh, 3, g_themeColor);

        uint16_t sc = symColor(entries[i].sym);

        // Draw 3 tiny suit symbols (or "7" for seven)
        bool isSmall = (entries[i].mult == 25);  // 25-box: smaller symbols
        int symOffset = isSmall ? -5 : 0;
        int symSz = isSmall ? 6 : 8;
        int starSz = isSmall ? 5 : 7;
        for (int j = 0; j < 3; j++) {
            int sx = ex + 8 + j * 18;
            int sy = ey + eh / 2 + symOffset;
            if (entries[i].sym == SYM_WILD) {
                drawStar(tft, sx, sy, starSz, sc);
            } else if (entries[i].sym == SYM_SEVEN) {
                tft.setTextFont(1);
                tft.setTextColor(sc, COL_BG);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("7", sx, sy);
            } else {
                drawSuitSymbol(tft, sx, sy, symSz, entries[i].sym);
            }
        }

        // Payout number
        tft.setTextFont(1);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setTextDatum(MR_DATUM);
        char buf[6];
        snprintf(buf, sizeof(buf), "%u", entries[i].mult);
        tft.drawString(buf, ex + ew - 4, ey + eh / 2);
    }
}

// Draw top bar with title and credits
static void drawTopBar(TFT_eSPI &tft, unsigned long creds) {
    tft.fillRect(0, 0, SCREEN_W, 22, COL_BG);
    tft.setTextFont(2);

    // "SLOTS" left-aligned
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("SLOTS", 4, 2);

    // Credits centered
    tft.setTextDatum(TC_DATUM);
    char buf[20];
    snprintf(buf, sizeof(buf), "CREDITS %lu", creds);
    tft.drawString(buf, SCREEN_W / 2, 2);
}

// Draw bet button (bottom-left corner, 10px from edges)
static void drawBetButton(TFT_eSPI &tft, bool spinning) {
    int bw = 100, bh = 28;
    int bx = 10;                  // 10px from left
    int by = SCREEN_H - bh - 10;  // 10px from bottom

    tft.fillRoundRect(bx, by, bw, bh, 5, COL_BG);
    uint16_t col = spinning ? COL_DIM_GRAY : g_themeColor;
    tft.drawRoundRect(bx, by, bw, bh, 5, col);
    tft.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 5, col);

    tft.setTextFont(2);
    tft.setTextColor(col, COL_BG);
    tft.setTextDatum(MC_DATUM);
    char buf[12];
    snprintf(buf, sizeof(buf), "BET %u", SLOT_BETS[g_slots.betIdx]);
    tft.drawString(buf, bx + bw / 2, by + bh / 2);
}

// Draw spin button (bottom-right corner, 10px from edges)
static void drawSpinButton(TFT_eSPI &tft, bool spinning, unsigned long credits) {
    int bw = 100, bh = 28;
    int bx = SCREEN_W - bw - 10;  // 10px from right
    int by = SCREEN_H - bh - 10;  // 10px from bottom

    bool canSpin = !spinning && credits >= SLOT_BETS[g_slots.betIdx];
    uint16_t col = canSpin ? g_themeColor : COL_DIM_GRAY;

    tft.fillRoundRect(bx, by, bw, bh, 5, COL_BG);
    tft.drawRoundRect(bx, by, bw, bh, 5, col);
    tft.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 5, col);

    tft.setTextFont(2);
    tft.setTextColor(col, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SPIN", bx + bw / 2, by + bh / 2);
}

// Draw status message — always below top bar (under credits), same font
static void drawWinMessage(TFT_eSPI &tft) {
    int msgY = 19;  // below top bar
    tft.fillRect(0, 22, SCREEN_W, 14, COL_BG);

    tft.setTextFont(2);
    tft.setTextDatum(TC_DATUM);

    if (g_slots.winAmount > 0) {
        tft.setTextColor(COL_GOLD, COL_BG);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s  +%lu", g_slots.winLabel, g_slots.winAmount);
        tft.drawString(buf, SCREEN_W / 2, msgY);
    } else {
        tft.setTextColor(COL_DIM_GRAY, COL_BG);
        tft.drawString("NO WIN — SPIN AGAIN!", SCREEN_W / 2 + 10, msgY);
    }
}

// Draw gamble sub-screen (COLLECT / LOW / HIGH buttons + face-down card)
static void drawGambleUI(TFT_eSPI &tft) {
    // Buttons positioned above the bottom row
    int btnW = 100, btnH = 28;
    int btnX = SCREEN_W / 2 - btnW / 2;
    int baseY = SCREEN_H - btnH - 10;  // same row as SPIN

    // COLLECT button (centered, above LOW/HIGH)
    int cy = baseY - btnH - 8;
    tft.fillRoundRect(btnX, cy, btnW, btnH, 5, COL_BG);
    tft.drawRoundRect(btnX, cy, btnW, btnH, 5, g_themeColor);
    tft.drawRoundRect(btnX + 1, cy + 1, btnW - 2, btnH - 2, 5, g_themeColor);
    tft.setTextFont(2);
    tft.setTextColor(g_themeColor, COL_BG);
    tft.setTextDatum(MC_DATUM);
    char buf[24];
    snprintf(buf, sizeof(buf), "COLLECT %lu", g_slots.gambleAmount);
    tft.drawString(buf, btnX + btnW / 2, cy + btnH / 2);

    // LOW / HIGH buttons (side by side below COLLECT)
    int lhY = baseY;
    int lhW = (btnW - 6) / 2;
    for (int i = 0; i < 2; i++) {
        int lx = btnX + i * (lhW + 6);
        tft.fillRoundRect(lx, lhY, lhW, btnH, 5, COL_BG);
        tft.drawRoundRect(lx, lhY, lhW, btnH, 5, g_themeColor);
        tft.drawRoundRect(lx + 1, lhY + 1, lhW - 2, btnH - 2, 5, g_themeColor);
        tft.setTextFont(2);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.drawString(i == 0 ? "LOW" : "HIGH", lx + lhW / 2, lhY + btnH / 2);
    }

    // Face-down card in center reel area
    int cardX = SLOT_REEL_X0 + SLOT_REEL_W + SLOT_REEL_GAP + (SLOT_REEL_W - 56) / 2;
    int cardY = SLOT_REEL_Y + (SLOT_REEL_H - 82) / 2;
    drawCardBack(tft, cardX, cardY);

    // Gamble pot display above the card
    tft.setTextFont(2);
    tft.setTextColor(COL_GOLD, COL_BG);
    tft.setTextDatum(TC_DATUM);
    snprintf(buf, sizeof(buf), "POT: %lu", g_slots.gambleAmount);
    tft.drawString(buf, SCREEN_W / 2, cardY - 12);
}

// ── Win evaluation ────────────────────────────────────────────────────────

// Check if two symbols match (considering wild as universal matcher)
static bool symMatch(uint8_t a, uint8_t b) {
    if (a == SYM_WILD || b == SYM_WILD) return true;
    return a == b;
}

// Returns payout index (0-10) or -1 for no win
static int evaluateWin(uint8_t s0, uint8_t s1, uint8_t s2, unsigned long bet) {
    int wilds = (s0 == SYM_WILD) + (s1 == SYM_WILD) + (s2 == SYM_WILD);
    int sevens = (s0 == SYM_SEVEN) + (s1 == SYM_SEVEN) + (s2 == SYM_SEVEN);

    // Three of a kind (with wilds)
    if (wilds == 3) return 0;                    // ★★★
    if (sevens == 3) return 1;                   // 777
    if (s0 == s1 && s1 == s2) {
        if (s0 == SYM_SPADE)   return 2;          // ♠♠♠
        if (s0 == SYM_HEART)   return 3;          // ♥♥♥
        if (s0 == SYM_DIAMOND) return 4;          // ♦♦♦
        if (s0 == SYM_CLUB)    return 5;          // ♣♣♣
    }

    // Two wilds + anything
    if (wilds == 2) return 6;

    // Two sevens
    if (sevens == 2 && wilds == 0) return 7;

    // One wild + pair
    if (wilds == 1) {
        uint8_t a = (s0 != SYM_WILD) ? s0 : s1;
        uint8_t b = (s0 != SYM_WILD && s1 != SYM_WILD) ? s1 : s2;
        if (a == SYM_SEVEN || b == SYM_SEVEN) return 7;  // 7+7+★ → 5× (treat as two sevens)
        if (a == b) return 8;   // pair + wild
        return -1;              // wild + two different → no win
    }

    // Any pair (no wilds)
    if (s0 == s1 || s1 == s2 || s0 == s2) {
        if ((s0 == s1 && (s0 == SYM_SEVEN)) ||
            (s1 == s2 && (s1 == SYM_SEVEN)) ||
            (s0 == s2 && (s0 == SYM_SEVEN))) return 7;  // two sevens
        return 10;  // any pair
    }

    return -1;
}

// ── Public API ─────────────────────────────────────────────────────────────

void slotsInit() {
    memset(&g_slots, 0, sizeof(g_slots));
    g_slots.phase = SLOT_IDLE;

    // Initialize reels at random offsets so they don't all look identical
    for (int i = 0; i < 3; i++) {
        g_slots.reels[i].pos = random(SLOT_REEL_LENGTH);
        g_slots.reels[i].spinning = false;
    }
    g_slots.betIdx = 1;  // default bet: 3
}

void slotsDraw(TFT_eSPI &tft, unsigned long credits) {
    tft.fillScreen(COL_BG);

    drawTopBar(tft, credits);

    // Reels
    for (int i = 0; i < 3; i++) {
        int rx = SLOT_REEL_X0 + i * (SLOT_REEL_W + SLOT_REEL_GAP);
        drawReel(tft, i, rx);
    }

    drawMiniPaytable(tft);

    bool spinning = slotsIsSpinning();

    if (g_slots.gambling) {
        drawGambleUI(tft);
    } else {
        drawBetButton(tft, spinning);
        drawSpinButton(tft, spinning, credits);

        if (g_slots.phase == SLOT_EVALUATE) {
            drawWinMessage(tft);
        }
    }

    // Back button (always visible, top-left)
    {
        tft.fillRoundRect(HM_BACK_X, HM_BACK_Y, 80, HM_BACK_H, 4, COL_BG);
        tft.drawRoundRect(HM_BACK_X, HM_BACK_Y, 80, HM_BACK_H, 4, g_themeColor);
        tft.drawRoundRect(HM_BACK_X + 1, HM_BACK_Y + 1, 78, HM_BACK_H - 2, 4, g_themeColor);
        tft.setTextFont(1);
        tft.setTextColor(g_themeColor, COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("VIDEO POKER", HM_BACK_X + 40, HM_BACK_Y + HM_BACK_H / 2);
    }

    // Power button (top-right, same position as other screens)
    {
        int px = 306, py = 10, pr = 7;
        tft.drawCircle(px, py, pr, g_themeColor);
        tft.drawCircle(px, py, pr - 1, g_themeColor);
        tft.drawLine(px, py - pr + 3, px, py - 1, g_themeColor);
        tft.drawLine(px - 1, py - pr + 3, px + 1, py - pr + 3, g_themeColor);
    }
}

bool slotsTap(TFT_eSPI &tft, int16_t tx, int16_t ty) {
    // Never accept taps while reels are spinning
    if (slotsIsSpinning()) return false;

    // ── VIDEO POKER back button ──────────────────────────────────────
    if (tx >= HM_BACK_X && tx <= HM_BACK_X + 80 &&
        ty >= HM_BACK_Y && ty <= HM_BACK_Y + HM_BACK_H) {
        return false;  // handled by main.cpp (mode switch)
    }

    // ── Gamble sub-mode ──────────────────────────────────────────────
    if (g_slots.gambling) {
        int btnW = 100, btnH = 28;
        int btnX = SCREEN_W / 2 - btnW / 2;
        int baseY = SCREEN_H - btnH - 10;

        // COLLECT button (above LOW/HIGH)
        int cy = baseY - btnH - 8;
        if (tx >= btnX && tx <= btnX + btnW &&
            ty >= cy && ty <= cy + btnH) {
            g_slots.winAmount += g_slots.gambleAmount;
            g_slots.gambling = false;
            g_slots.gambleAmount = 0;
            g_slots.phase = SLOT_IDLE;
            return true;
        }

        // LOW / HIGH buttons (bottom row)
        int lhY = baseY;
        int lhW = (btnW - 6) / 2;
        for (int i = 0; i < 2; i++) {
            int lx = btnX + i * (lhW + 6);
            if (tx >= lx && tx <= lx + lhW &&
                ty >= lhY && ty <= lhY + btnH) {
                // Gamble: pick a random card, LOW=2-7 wins, HIGH=9-A wins
                uint8_t card = random(52);
                uint8_t rank = cardRank(card);  // 0=Joker/J, 1=A, 2=2, ..., 13=K
                bool isHigh = (rank >= 9 || rank == 0 || rank == 1);  // 9,10,J,Q,K,A
                bool pickedHigh = (i == 1);
                bool win = (isHigh == pickedHigh);

                // Reveal card in center reel area
                int cardX = SLOT_REEL_X0 + SLOT_REEL_W + SLOT_REEL_GAP + (SLOT_REEL_W - 56) / 2;
                int cardY = SLOT_REEL_Y + (SLOT_REEL_H - 82) / 2;
                drawCardFace(tft, cardX, cardY, card);

                if (win) {
                    g_slots.gambleAmount *= 2;
                    // Redraw with updated pot
                    drawGambleUI(tft);
                } else {
                    g_slots.gambleAmount = 0;
                    g_slots.gambling = false;
                    g_slots.phase = SLOT_EVALUATE;  // back to win/loss (caller redraws)
                }
                return true;
            }
        }
        return false;
    }

    // ── SLOT_EVALUATE: tap win message under credits → gamble ─────
    if (g_slots.phase == SLOT_EVALUATE && g_slots.winAmount > 0) {
        // Message around y=19 (below top bar)
        if (ty >= 17 && ty <= 33 &&
            tx >= 40 && tx <= SCREEN_W - 40) {
            // Start gamble
            g_slots.gambling = true;
            g_slots.gambleAmount = g_slots.winAmount;
            g_slots.winAmount = 0;
            return true;
        }
    }

    // ── Bet button ───────────────────────────────────────────────────
    int bw = 100, bh = 28;
    int bx = 10;
    int by = SCREEN_H - bh - 10;
    if (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh) {
        g_slots.betIdx = (g_slots.betIdx + 1) % SLOT_BET_COUNT;
        if (g_slots.phase == SLOT_EVALUATE) {
            g_slots.phase = SLOT_IDLE;
            g_slots.winAmount = 0;
        }
        return true;
    }

    // ── SPIN button (bottom-right corner) ─────────────────────────────
    int sbw = 100, sbh = 28;
    int sbx = SCREEN_W - sbw - 10;
    int sby = SCREEN_H - sbh - 10;
    if (tx >= sbx && tx <= sbx + sbw && ty >= sby && ty <= sby + sbh) {
        uint8_t bet = SLOT_BETS[g_slots.betIdx];
        // Credit check handled by caller (main.cpp reads credits)
        // We just start the spin; main.cpp deducts credits
        if (g_slots.phase == SLOT_EVALUATE) {
            g_slots.winAmount = 0;
        }

        g_slots.phase = SLOT_SPINNING;
        g_slots.winAmount = 0;
        g_slots.winLabel[0] = '\0';
        g_slots.gambling = false;

        unsigned long now = millis();
        for (int i = 0; i < 3; i++) {
            ReelState &r = g_slots.reels[i];
            r.spinning    = true;
            r.stopAt      = now + 800 + i * 500 + random(400);   // L→R stagger
            r.stopTarget  = random(SLOT_REEL_LENGTH);
            r.lastAdvance = now;
            r.tickMs      = 45;    // fast spin
            r.decelStep   = 0;
        }
        return true;
    }

    return false;
}

void slotsAnimate(TFT_eSPI &tft, unsigned long credits) {
    if (g_slots.phase != SLOT_SPINNING) return;

    unsigned long now = millis();
    bool anySpinning = false;

    for (int i = 0; i < 3; i++) {
        ReelState &r = g_slots.reels[i];
        if (!r.spinning) continue;
        anySpinning = true;

        // ── Deceleration phases based on time relative to stopAt ──────────
        long remaining = (long)(r.stopAt - now);

        if (r.decelStep == 0 && remaining <= 0) {
            // Enter deceleration — slow down over 4 ticks
            r.decelStep = 1;
            r.tickMs = 80;
        } else if (r.decelStep == 1 && remaining <= -120) {
            r.decelStep = 2;
            r.tickMs = 130;
        } else if (r.decelStep == 2 && remaining <= -300) {
            r.decelStep = 3;
            r.tickMs = 200;
        } else if (r.decelStep == 3 && remaining <= -560) {
            r.decelStep = 4;  // final snap
        }

        // ── Advance reel by one position when tick fires ────────────────
        if (now - r.lastAdvance >= (unsigned long)r.tickMs) {
            r.pos = (r.pos + 1) % SLOT_REEL_LENGTH;
            r.lastAdvance = now;

            // Redraw this reel
            int rx = SLOT_REEL_X0 + i * (SLOT_REEL_W + SLOT_REEL_GAP);
            drawReel(tft, i, rx);

            // After decel step 4, snap to exact target and stop
            if (r.decelStep == 4) {
                int targetTop = (r.stopTarget - 1 + SLOT_REEL_LENGTH) % SLOT_REEL_LENGTH;
                r.pos = targetTop;
                r.spinning = false;
                // Final redraw at exact position
                drawReel(tft, i, rx);
            }
        }
    }

    if (!anySpinning) {
        // All reels stopped — evaluate win
        uint8_t s[3];
        for (int i = 0; i < 3; i++) {
            int payIdx = (g_slots.reels[i].pos + 1) % SLOT_REEL_LENGTH;
            s[i] = REEL_STRIP[payIdx];
        }
        uint8_t bet = SLOT_BETS[g_slots.betIdx];
        int winIdx = evaluateWin(s[0], s[1], s[2], bet);
        if (winIdx >= 0) {
            g_slots.winAmount = (unsigned long)SLOT_PAYOUTS[winIdx].multiplier * bet;
            snprintf(g_slots.winLabel, sizeof(g_slots.winLabel), "%s", SLOT_PAYOUTS[winIdx].label);
        } else {
            g_slots.winAmount = 0;
        }
        g_slots.phase = SLOT_EVALUATE;
        slotsDraw(tft, credits);
    }
}

bool slotsIsSpinning() {
    if (g_slots.phase != SLOT_SPINNING) return false;
    for (int i = 0; i < 3; i++)
        if (g_slots.reels[i].spinning) return true;
    return false;
}
