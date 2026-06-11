/**
 * CYD-Poker — 5-card Draw Video Poker with Joker
 * Outlined theme-colored cards, visible buttons, theming system
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

static TFT_eSPI     tft;
static TFT_eSPI    *disp = &tft;  // drawing target — normally &tft, swapped to sprite for capture
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "config.h"
#include "theme.h"
#include "cards.h"
#include "holdem.h"

static SPIClass    touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS);

static int16_t  tx = 0, ty = 0;
static bool     touched = false;
static bool     wasTouched = false;

static unsigned long credits = STARTING_CREDITS;
static uint8_t  gamePhase = 0;
static int8_t   win = -1;
static unsigned long payout = 0;
static uint8_t  hand[5];
static bool     hold[5] = {false};
static bool     used[60] = {false};
static uint8_t  jokers = 0;
static uint8_t  dice = 0;
static uint8_t  gameMode = 0;      // 0=5-card draw, 1=Texas Hold'em


// ── NVS convenience wrappers ────────────────────────────────────────────────

static int32_t nvsGetInt(const char *key, int32_t def) {
    Preferences p; p.begin("cyd-poker", true);
    int32_t v = p.getInt(key, def); p.end();
    return v;
}
static void nvsPutInt(const char *key, int32_t val) {
    Preferences p; p.begin("cyd-poker", false);
    p.putInt(key, val); p.end();
}

// ── Touch ──────────────────────────────────────────────────────────────────

static int s_touchRotation = 2;   // 0-3, NVS-backed; default 2 (180°) for 2USB

// Raw coordinate translation: XPT2046 raw → screen pixels
static bool rawToScreen(int16_t *sx, int16_t *sy) {
    if (!ts.touched()) return false;
    TS_Point p = ts.getPoint();
    *sx = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W - 1);
    *sy = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H - 1, 0);
#if CYD_USB_VERSION == 2
    // 2USB display has MY mirror: both axes need flipping for touch
    *sx = SCREEN_W - 1 - *sx;
    *sy = SCREEN_H - 1 - *sy;
#endif
    *sx = constrain(*sx, 0, SCREEN_W - 1);
    *sy = constrain(*sy, 0, SCREEN_H - 1);
    return true;
}

// Edge-triggered read — for game button detection (rising edge only)
static bool readTouch() {
    int16_t rx, ry;
    bool nowTouched = rawToScreen(&rx, &ry);
    if (!nowTouched) { wasTouched = false; touched = false; return false; }
    tx = rx; ty = ry;
    touched = true;
    if (wasTouched) return false;
    wasTouched = true;
    return true;
}

// Level-triggered read — for calibration live cursor polling
static bool touchIsHeld(int16_t *ox = nullptr, int16_t *oy = nullptr) {
    int16_t rx, ry;
    bool t = rawToScreen(&rx, &ry);
    if (t) {
        if (ox) *ox = rx;
        if (oy) *oy = ry;
    }
    return t;
}

static void touchSetRotation(int rotation) {
    s_touchRotation = rotation & 3;
    nvsPutInt("touch_rot", s_touchRotation);
    ts.setRotation(s_touchRotation);
}

static int touchGetRotation() { return s_touchRotation; }

// ── Credit persistence ─────────────────────────────────────────────────────

static void saveCredits() {
    Preferences prefs;
    prefs.begin("cyd-poker", false);
    prefs.putULong("credits", credits);
    prefs.end();
}

static void loadCredits() {
    Preferences prefs;
    prefs.begin("cyd-poker", true);
    credits = prefs.getULong("credits", STARTING_CREDITS);
    if (credits < BET) credits = STARTING_CREDITS;  // ensure playable
    prefs.end();
}

// ── Power button ───────────────────────────────────────────────────────────

static void drawPowerButton() {
    // Circle outline
    disp->drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R, g_themeColor);
    disp->drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R - 1, g_themeColor);
    // Power icon: vertical line at top with gap from circle edge
    disp->drawLine(PWR_BTN_X, PWR_BTN_Y - PWR_BTN_R + 3, PWR_BTN_X, PWR_BTN_Y - 1, g_themeColor);
    disp->drawLine(PWR_BTN_X - 1, PWR_BTN_Y - PWR_BTN_R + 3, PWR_BTN_X + 1, PWR_BTN_Y - PWR_BTN_R + 3, g_themeColor);
}

static bool hitPowerButton() {
    int dx = tx - PWR_BTN_X;
    int dy = ty - PWR_BTN_Y;
    return (dx * dx + dy * dy <= (PWR_BTN_R + 4) * (PWR_BTN_R + 4));
}

static void goToSleep() {
    saveCredits();
    // Show sleep message briefly
    disp->fillScreen(COL_BG);
    disp->setTextFont(2);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    disp->drawString("SLEEP", SCREEN_W / 2, SCREEN_H / 2);
    delay(500);

    // Deep sleep — wake on touch IRQ (GPIO 36, active low)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
    esp_deep_sleep_start();
}

// ── Drawing helpers ────────────────────────────────────────────────────────

static void drawRightNumber(int x, int y, unsigned long num) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", num);
    disp->setTextDatum(TR_DATUM);
    disp->drawString(buf, x, y);
}

static void drawCenterText(int x, int y, const char* s) {
    disp->setTextDatum(MC_DATUM);
    disp->drawString(s, x, y);
}

// ── Paytable ───────────────────────────────────────────────────────────────

static void drawPayTable() {
    disp->setTextFont(1);

    for (int i = 0; i < 10; i++) {
        int rowY = PT_Y + i * PT_LINE_H;
        uint16_t rowBg = (i % 2 == 0) ? COL_BG : 0x1082;
        disp->fillRect(PT_X, rowY, PT_W, PT_LINE_H, rowBg);

        disp->setTextDatum(TL_DATUM);
        disp->setTextColor(g_themeColor, rowBg);
        disp->drawString(WIN_NAMES[i], PT_X + 2, rowY + 1);

        disp->setTextColor(g_themeColor, rowBg);
        drawRightNumber(PT_RX - 2, rowY + 1, (unsigned long)PAYOUTS[i] * BET);
    }

    disp->drawFastVLine(RIGHT_X - 4, PT_Y, PT_H, COL_DIM_GRAY);
}

// ── Credits (right panel) ──────────────────────────────────────────────────

static void drawCredits() {
    disp->fillRect(RIGHT_X, 2, RIGHT_W, 54, COL_BG);
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(TC_DATUM);
    disp->drawString("CREDITS", CREDITS_CX, 4);
    disp->drawString(g_themes[g_themeIdx].name, CREDITS_CX, 14);
    disp->setTextFont(4);
    disp->setTextColor(g_themeColor, COL_BG);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", credits);
    disp->setTextDatum(TC_DATUM);
    disp->drawString(buf, CREDITS_CX, 24);
    drawPowerButton();  // restore after clearing right panel
}

// ── Action button (right panel) ────────────────────────────────────────────

static void drawActionButton(const char* label) {
    // Black fill, themed outline + themed text
    disp->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 5, COL_BG);
    disp->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 5, g_themeColor);
    disp->drawRoundRect(BTN_X + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, 5, g_themeColor);
    disp->setTextFont(2);
    disp->setTextColor(g_themeColor, COL_BG);
    drawCenterText(BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2, label);
}

static bool hitActionButton() {
    return (tx >= BTN_X && tx <= BTN_X + BTN_W &&
            ty >= BTN_Y && ty <= BTN_Y + BTN_H);
}

// ── Gamble buttons ─────────────────────────────────────────────────────────

static void drawGambleButtons() {
    const char* labels[] = {"COLLECT", "LOW", "HIGH"};

    for (int i = 0; i < 3; i++) {
        int by = GMBL_Y0 + i * GMBL_GAP;
        disp->fillRoundRect(GMBL_X, by, GMBL_W, GMBL_H, 4, COL_BG);
        disp->drawRoundRect(GMBL_X, by, GMBL_W, GMBL_H, 4, g_themeColor);
        disp->drawRoundRect(GMBL_X + 1, by + 1, GMBL_W - 2, GMBL_H - 2, 4, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(GMBL_X + GMBL_W / 2, by + GMBL_H / 2, labels[i]);
    }
}

static int hitGambleButton() {
    if (tx < GMBL_X || tx > GMBL_X + GMBL_W) return -1;
    for (int i = 0; i < 3; i++) {
        int by = GMBL_Y0 + i * GMBL_GAP;
        if (ty >= by && ty <= by + GMBL_H) return i;
    }
    return -1;
}

// ── Message bar ────────────────────────────────────────────────────────────

static void drawMessage(const char* msg, uint16_t col) {
    disp->fillRect(0, MSG_Y, SCREEN_W, MSG_H, COL_BG);
    disp->setTextFont(2);
    disp->setTextColor(col, COL_BG);
    drawCenterText(SCREEN_W / 2, MSG_Y + MSG_H / 2, msg);
}

// ── Win box ────────────────────────────────────────────────────────────────

static void clearWinBox() {
    disp->fillRect(PT_X, PT_Y, PT_W, PT_H, COL_WIN_BG);
    disp->drawRect(PT_X, PT_Y, PT_W, PT_H, g_themeColor);
}

static void updatePayout() {
    disp->setTextFont(4);
    disp->setTextColor(COL_WHITE, COL_WIN_BG);
    disp->setTextDatum(MC_DATUM);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", payout);
    disp->drawString(buf, PT_X + PT_W / 2, PT_Y + PT_H / 2 + 6);
}

static void highlightWin(uint16_t col) {
    if (win < 0) return;
    int rowY = PT_Y + win * PT_LINE_H;
    disp->setTextFont(1);
    disp->setTextDatum(TL_DATUM);
    disp->setTextColor(col, COL_BG);
    disp->drawString(WIN_NAMES[win], PT_X + 2, rowY + 1);
    drawRightNumber(PT_RX - 2, rowY + 1, payout);
}

// ── Empty card slots ───────────────────────────────────────────────────────

static void drawEmptySlots() {
    for (int i = 0; i < 5; i++) {
        int cx = PAYTABLE_X + i * CARD_GAP;
        uint16_t dim = COL_BG;
        disp->fillRoundRect(cx, CARD_Y, CARD_W, CARD_H, 5, dim);
        disp->drawRoundRect(cx, CARD_Y, CARD_W, CARD_H, 5, g_themeColor);
    }
}

// ── Mode toggle button ─────────────────────────────────────────────────────

static void drawModeToggle() {
    const char* label = "HOLD'EM";
    disp->fillRoundRect(MODE_BTN_X, MODE_BTN_Y, MODE_BTN_W, MODE_BTN_H, 5, COL_BG);
    disp->drawRoundRect(MODE_BTN_X, MODE_BTN_Y, MODE_BTN_W, MODE_BTN_H, 5, g_themeColor);
    disp->drawRoundRect(MODE_BTN_X + 1, MODE_BTN_Y + 1, MODE_BTN_W - 2, MODE_BTN_H - 2, 5, g_themeColor);
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    drawCenterText(MODE_BTN_X + MODE_BTN_W / 2, MODE_BTN_Y + MODE_BTN_H / 2, label);
}

static bool hitModeToggle() {
    return (tx >= MODE_BTN_X && tx <= MODE_BTN_X + MODE_BTN_W &&
            ty >= MODE_BTN_Y && ty <= MODE_BTN_Y + MODE_BTN_H);
}

// ── Texas Hold'em screen ───────────────────────────────────────────────────

static void drawSmallCardFace(int x, int y, uint8_t card) {
    uint8_t rank = cardRank(card);
    uint8_t suit = cardSuit(card);
    uint16_t col = suitColor(suit);
    uint16_t fill = COL_BG;

    disp->fillRoundRect(x, y, HCARD_W, HCARD_H, 4, fill);
    disp->drawRoundRect(x, y, HCARD_W, HCARD_H, 4, col);

    if (rank == 0) {
        disp->setTextFont(1);
        disp->setTextColor(g_themeColor, fill);
        disp->setTextDatum(MC_DATUM);
        disp->drawString("J", x + HCARD_W/2, y + HCARD_H/2);
        return;
    }

    // Corner rank (top-left)
    disp->setTextFont(1);
    disp->setTextColor(col, fill);
    disp->setTextDatum(TL_DATUM);
    disp->drawString(rankStr(rank), x + 3, y + 2);

    // Corner rank (bottom-right)
    disp->setTextDatum(BR_DATUM);
    disp->drawString(rankStr(rank), x + HCARD_W - 3, y + HCARD_H - 2);

    // Center suit
    drawSuitSymbol(tft, x + HCARD_W/2, y + HCARD_H/2 + 2, 12, suit);
}

static void drawSmallCardBack(int x, int y) {
    uint16_t fill = COL_BG;
    disp->fillRoundRect(x, y, HCARD_W, HCARD_H, 4, fill);
    disp->drawRoundRect(x, y, HCARD_W, HCARD_H, 4, g_themeColor);
    // Cross-hatch
    for (int cy = y + 4; cy < y + HCARD_H - 4; cy += 6)
        for (int cx = x + 4; cx < x + HCARD_W - 4; cx += 6)
            disp->fillRect(cx, cy, 2, 2, (g_themeColor >> 1) & 0x7BEF);
}

static void drawHoldemScreen() {
    disp->fillScreen(COL_BG);
    char buf[40];

    drawPowerButton();

    // ── Back to Video Poker button (top-left) ──
    {
        disp->fillRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, COL_BG);
        disp->drawRoundRect(HM_BACK_X, HM_BACK_Y, HM_BACK_W, HM_BACK_H, 4, g_themeColor);
        disp->drawRoundRect(HM_BACK_X + 1, HM_BACK_Y + 1, HM_BACK_W - 2, HM_BACK_H - 2, 4, g_themeColor);
        disp->setTextFont(1);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(HM_BACK_X + HM_BACK_W / 2, HM_BACK_Y + HM_BACK_H / 2, "VIDEO POKER");
    }

    // ── Positions ──
    int aiY     = 8;    // AI cards
    int plyY    = 186;  // player cards
    int commY   = aiY + HCARD_H + (plyY - (aiY + HCARD_H) - HCCARD_H) / 2;
    int cardCX  = SCREEN_W / 2 + 14;
    int pc1x = cardCX - HCARD_W - 4;
    int pc2x = cardCX + 4;
    int aiCard1X = pc1x;
    int aiCard2X = pc2x;

    // ── AI cards + info (top-left) ──
    if (g_hm.stage >= HM_PREFLOP) {
        disp->setTextFont(1);
        disp->setTextDatum(TL_DATUM);

        // Name + stack
        disp->setTextColor(g_themeColor, COL_BG);
        snprintf(buf, sizeof(buf), "xXSmokeXx:%lu", g_hm.aiStack);
        disp->drawString(buf, 4, aiY + 24);

        // AI status
        if (g_hm.stage < HM_HAND_OVER) {
            disp->setTextColor(g_themeColor, COL_BG);
            disp->drawString(holdemAIStatus(), 4, aiY + 36);
        }

        // AI last action (fold/raise/call + amounts)
        if (g_hm.aiLastAction[0]) {
            disp->setTextColor(g_themeColor, COL_BG);
            disp->drawString(g_hm.aiLastAction, 4, aiY + 48);
        }

        // AI cards (centered)
        if (g_hm.stage >= HM_SHOWDOWN) {
            drawSmallCardFace(aiCard1X, aiY, g_hm.aiCards[0]);
            drawSmallCardFace(aiCard2X, aiY, g_hm.aiCards[1]);
        } else {
            drawSmallCardBack(aiCard1X, aiY);
            drawSmallCardBack(aiCard2X, aiY);
        }
    }

    // ── POT + Blinds (top-right, flush right) ──
    disp->setTextFont(1);
    disp->setTextDatum(TR_DATUM);
    disp->setTextColor(g_themeColor, COL_BG);
    snprintf(buf, sizeof(buf), "POT:%lu", g_hm.pot);
    disp->drawString(buf, SCREEN_W - 4, aiY + 24);
    disp->setTextColor(g_themeColor, COL_BG);
    snprintf(buf, sizeof(buf), "Blinds:%d/%d%s", HOLDEM_SB, HOLDEM_BB,
             g_hm.playerDealer ? " D" : "");
    disp->drawString(buf, SCREEN_W - 4, aiY + 36);

    drawPowerButton();

    // ── Community cards (bigger than hole cards) ──
    int ccGap = 57;  // tight spacing for 5 big cards
    int ccx0 = cardCX - (5 * HCCARD_W + 4 * (ccGap - HCCARD_W)) / 2;
    if (ccx0 < 2) ccx0 = 2;

    for (int i = 0; i < 5; i++) {
        int cx = ccx0 + i * ccGap;
        uint16_t fill = COL_BG;
        if (g_hm.communityRevealed > i) {
            uint8_t rank = cardRank(g_hm.community[i]);
            uint8_t suit = cardSuit(g_hm.community[i]);
            uint16_t col = suitColor(suit);
            disp->fillRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, fill);
            disp->drawRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, col);
            disp->setTextFont(1);
            disp->setTextColor(col, fill);
            disp->setTextDatum(TL_DATUM);
            disp->drawString(rankStr(rank), cx + 3, commY + 3);
            disp->setTextDatum(BR_DATUM);
            disp->drawString(rankStr(rank), cx + HCCARD_W - 3, commY + HCCARD_H - 3);
            drawSuitSymbol(tft, cx + HCCARD_W/2, commY + HCCARD_H/2 + 2, 14, suit);
        } else if (g_hm.stage >= HM_PREFLOP) {
            disp->fillRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, fill);
            disp->drawRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, g_themeColor);
            for (int cy = commY + 4; cy < commY + HCCARD_H - 4; cy += 6)
                for (int cx2 = cx + 4; cx2 < cx + HCCARD_W - 4; cx2 += 6)
                    disp->fillRect(cx2, cy, 2, 2, (g_themeColor >> 1) & 0x7BEF);
        } else {
            disp->fillRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, fill);
            disp->drawRoundRect(cx, commY, HCCARD_W, HCCARD_H, 4, g_themeColor);
        }
    }

    // Adjust commY for bigger cards in the stage label calc
    int commH = HCCARD_H;

    // ── Stage label + last action (between community and player) ──
    disp->setTextFont(1);
    disp->setTextDatum(TC_DATUM);
    const char* stageName = "";
    switch (g_hm.stage) {
        case HM_PREFLOP: stageName="PRE-FLOP"; break;
        case HM_FLOP:    stageName="FLOP"; break;
        case HM_TURN:    stageName="TURN"; break;
        case HM_RIVER:   stageName="RIVER"; break;
        case HM_SHOWDOWN:stageName="SHOWDOWN"; break;
        case HM_HAND_OVER:stageName="HAND OVER"; break;
        default: break;
    }
    int midCardCX = ccx0 + 2 * ccGap + HCCARD_W / 2;
    int gapTop = commY + commH;
    int stageY = (gapTop + plyY) / 2;  // centered between community and player

    disp->fillRect(midCardCX - 70, stageY - 6, 140, 16, COL_BG);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->drawString(stageName, midCardCX, stageY);

    // ── Player cards + info (bottom) ──
    disp->setTextFont(1);
    disp->setTextDatum(TL_DATUM);

    disp->setTextColor(g_themeColor, COL_BG);
    snprintf(buf, sizeof(buf), "YOU:%lu", g_hm.playerStack);
    disp->drawString(buf, 4, plyY + 6);

    if (g_hm.playerBet > 0) {
        disp->setTextColor(g_themeColor, COL_BG);
        snprintf(buf, sizeof(buf), "BET:%lu", g_hm.playerBet);
        disp->drawString(buf, 4, plyY + 18);
    }

    if (g_hm.stage >= HM_PREFLOP) {
        drawSmallCardFace(pc1x, plyY, g_hm.playerCards[0]);
        drawSmallCardFace(pc2x, plyY, g_hm.playerCards[1]);
    } else {
        uint16_t dim = COL_BG;
        disp->fillRoundRect(pc1x, plyY, HCARD_W, HCARD_H, 3, dim);
        disp->drawRoundRect(pc1x, plyY, HCARD_W, HCARD_H, 3, g_themeColor);
        disp->fillRoundRect(pc2x, plyY, HCARD_W, HCARD_H, 3, dim);
        disp->drawRoundRect(pc2x, plyY, HCARD_W, HCARD_H, 3, g_themeColor);
    }

    // ── Action buttons ──
    if (g_hm.stage == HM_IDLE) {
        int dW = 80, dH = 28;
        int dX = cardCX - dW / 2;
        disp->fillRoundRect(dX, commY + 10, dW, dH, 6, COL_BG);
        disp->drawRoundRect(dX, commY + 10, dW, dH, 6, g_themeColor);
        disp->drawRoundRect(dX + 1, commY + 11, dW - 2, dH - 2, 6, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(cardCX, commY + 10 + dH / 2, "DEAL");

    } else if (g_hm.stage >= HM_PREFLOP && g_hm.stage <= HM_RIVER && !g_hm.playerFolded) {
        unsigned long toCall = g_hm.currentBet - g_hm.playerBet;
        const char* callLabel = (toCall == 0) ? "CHECK" : "CALL";
        int btnH = HCARD_H - 6;
        int rh = (btnH - 4) / 2;

        // FOLD + RESET — left of player cards, stacked (same style as right side)
        int foldX = pc1x - HM_SIDE_BTN_W - 8;
        disp->fillRoundRect(foldX, plyY + 2, HM_SIDE_BTN_W, rh, 4, COL_BG);
        disp->drawRoundRect(foldX, plyY + 2, HM_SIDE_BTN_W, rh, 4, g_themeColor);
        disp->drawRoundRect(foldX + 1, plyY + 3, HM_SIDE_BTN_W - 2, rh - 2, 4, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(foldX + HM_SIDE_BTN_W / 2, plyY + 2 + rh / 2, "FOLD");

        // RESET
        int resetY = plyY + 2 + rh + 4;
        disp->fillRoundRect(foldX, resetY, HM_SIDE_BTN_W, rh, 4, COL_BG);
        disp->drawRoundRect(foldX, resetY, HM_SIDE_BTN_W, rh, 4, g_themeColor);
        disp->drawRoundRect(foldX + 1, resetY + 1, HM_SIDE_BTN_W - 2, rh - 2, 4, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(foldX + HM_SIDE_BTN_W / 2, resetY + rh / 2, "RESET");

        // CALL+RAISE — right of player cards, stacked
        int rightX = pc2x + HCARD_W + 8;
        int rw = HM_SIDE_BTN_W + 8;

        disp->fillRoundRect(rightX, plyY + 2, rw, rh, 4, COL_BG);
        disp->drawRoundRect(rightX, plyY + 2, rw, rh, 4, g_themeColor);
        disp->drawRoundRect(rightX + 1, plyY + 3, rw - 2, rh - 2, 4, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(rightX + rw / 2, plyY + 2 + rh / 2, callLabel);

        int raiseY = plyY + 2 + rh + 4;
        disp->fillRoundRect(rightX, raiseY, rw, rh, 4, COL_BG);
        disp->drawRoundRect(rightX, raiseY, rw, rh, 4, g_themeColor);
        disp->drawRoundRect(rightX + 1, raiseY + 1, rw - 2, rh - 2, 4, g_themeColor);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(rightX + rw / 2, raiseY + rh / 2, "RAISE");

    } else if (g_hm.stage == HM_HAND_OVER) {
        int btnW = HCARD_W * 2 + 6, btnH = 24;
        int btnX = cardCX - btnW / 2;
        int btnY = plyY + HCARD_H + 4;
        if (btnY > 214) btnY = 214;
        disp->fillRoundRect(btnX, btnY, btnW, btnH, 6, COL_BG);
        disp->drawRoundRect(btnX, btnY, btnW, btnH, 6, g_themeColor);
        disp->drawRoundRect(btnX + 1, btnY + 1, btnW - 2, btnH - 2, 6, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(btnX + btnW / 2, btnY + btnH / 2, "NEXT HAND");

        // Result text — clear gap, draw centered
        if (g_hm.lastAction[0]) {
            int gapTop = commY + HCCARD_H;
            int gapH = plyY - gapTop;
            disp->fillRect(0, gapTop, SCREEN_W, gapH, COL_BG);
            disp->setTextFont(2);
            disp->setTextColor(g_themeColor, COL_BG);
            drawCenterText(cardCX, gapTop + gapH / 2, g_hm.lastAction);
        }
        credits = g_hm.playerStack;
        saveCredits();
    }
}

// ── Full redraw ────────────────────────────────────────────────────────────

static void redrawAll() {
    if (gameMode == 1) {
        drawHoldemScreen();
        return;
    }

    disp->fillScreen(COL_BG);

    drawPayTable();
    drawCredits();
    drawModeToggle();
    drawPowerButton();

    if (gamePhase == 0) {
        // Centered DEAL button — below paytable, above cards
        int dealW = 80, dealH = 28;
        int dealX = SCREEN_W / 2 - dealW / 2;
        int dealY = 102;
        disp->fillRoundRect(dealX, dealY, dealW, dealH, 6, COL_BG);
        disp->drawRoundRect(dealX, dealY, dealW, dealH, 6, g_themeColor);
        disp->drawRoundRect(dealX + 1, dealY + 1, dealW - 2, dealH - 2, 6, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(SCREEN_W / 2, dealY + dealH / 2, "DEAL");
        // Mode toggle stays in right panel
        drawModeToggle();
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
        drawEmptySlots();

    } else if (gamePhase == 1) {
        drawActionButton("DRAW");
        if (win >= 0) highlightWin(g_themeColor);
        drawMessage("TAP  CARDS  TO  HOLD  .  THEN  DRAW", g_themeColor);
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
        for (int i = 0; i < 5; i++) {
            int cx = PAYTABLE_X + i * CARD_GAP;
            drawCardFace(tft, cx, CARD_Y, hand[i]);
            drawHoldFrame(tft, i, hold[i], false);
        }

    } else if (gamePhase == 2) {
        drawActionButton("COLLECT");
        clearWinBox();
        disp->setTextFont(2);
        disp->setTextColor(COL_WHITE, COL_WIN_BG);
        drawCenterText(PT_X + PT_W / 2, PT_Y + PT_H / 3, WIN_NAMES[win]);
        updatePayout();
        drawMessage("COLLECT   OR   TAP  SCORE  TO  DOUBLE", g_themeColor);

    } else if (gamePhase == 3) {
        drawGambleButtons();
        drawMessage("PICK  LOW  OR  HIGH  .  COLLECT  TO  KEEP", g_themeColor);
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
        drawCardBack(tft, PT_X + 2 * CARD_GAP, CARD_Y);

    } else if (gamePhase == 4) {
        clearWinBox();
        disp->setTextFont(2);
        disp->setTextColor(COL_WHITE, COL_WIN_BG);
        drawCenterText(PT_X + PT_W / 2, PT_Y + PT_H / 3, "GAME OVER");
        updatePayout();
        drawActionButton("NEW GAME");
        drawMessage("PRESS  NEW  GAME  TO  CONTINUE", g_themeColor);
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
    }
}

// ── Theme notification ─────────────────────────────────────────────────────

static void showThemeName() {
    // Briefly show theme name in message area
    disp->fillRect(RIGHT_X, 2, RIGHT_W, 54, COL_BG);
    disp->setTextFont(2);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    disp->drawString(g_themes[g_themeIdx].name, CREDITS_CX, BTN_Y - 14);
    delay(600);
    drawCredits();
}

// ── Poker engine ───────────────────────────────────────────────────────────

static void evaluateWin(bool doHold);

static void dealHand() {
    for (int i = 0; i < 5; i++) {
        if (!hold[i]) {
            int j;
            do { j = random(52 + MAX_JOKERS * (jokers == 0)); }
            while (used[j]);
            if (j > 51) { jokers++; j = 52 + random(4); }
            hand[i] = (j % 4) + ((j / 4 + 1) % 14) * 4;
            used[j] = true;
        }
    }
    for (int i = 0; i < 5; i++) {
        int cx = PAYTABLE_X + i * CARD_GAP;
        if (!hold[i]) drawCardBack(tft, cx, CARD_Y);
        drawHoldFrame(tft, i, hold[i], true);
    }
    for (int i = 0; i < 5; i++) {
        int cx = PAYTABLE_X + i * CARD_GAP;
        drawCardFace(tft, cx, CARD_Y, hand[i]);
        if (disp == &tft) delay(50);
    }
}

static void startDeal() {
    if (credits < BET) {
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
        drawMessage("OUT  OF  CREDITS !", COL_RED);
        if (disp == &tft) delay(1000);
        payout = 100;
        gamePhase = 4;
        redrawAll();
        return;
    }
    credits -= BET;
    saveCredits();
    drawCredits();
    memset(hold, 0, sizeof(hold));
    memset(used, 0, sizeof(used));
    jokers = 0;
    disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
    dealHand();
    evaluateWin(true);
    if (win >= 0) highlightWin(g_themeColor);
    // Clear old centered DEAL button from idle screen
    disp->fillRect(SCREEN_W/2 - 40, 102, 80, 28, COL_BG);
    drawActionButton("DRAW");
    drawMessage("TAP  CARDS  TO  HOLD  .  THEN  DRAW", g_themeColor);
    gamePhase = 1;
}

static void finalDraw() {
    if (win >= 0) highlightWin(COL_WHITE);
    disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
    dealHand();
    evaluateWin(false);
    if (win >= 0) {
        clearWinBox();
        disp->setTextFont(2);
        disp->setTextColor(COL_WHITE, COL_WIN_BG);
        drawCenterText(PT_X + PT_W / 2, PT_Y + PT_H / 3, WIN_NAMES[win]);
        updatePayout();
        drawActionButton("COLLECT");
        drawMessage("COLLECT   OR   TAP  SCORE  TO  DOUBLE", g_themeColor);
        gamePhase = 2;
    } else {
        // Clear old DRAW button from right panel and message area
        disp->fillRect(BTN_X - 2, BTN_Y - 2, BTN_W + 4, BTN_H + 4, COL_BG);
        disp->fillRect(0, 100, SCREEN_W, 40, COL_BG);
        // Centered DEAL button
        disp->fillRoundRect(SCREEN_W/2 - 40, 106, 80, 28, 6, COL_BG);
        disp->drawRoundRect(SCREEN_W/2 - 40, 106, 80, 28, 6, g_themeColor);
        disp->drawRoundRect(SCREEN_W/2 - 40 + 1, 107, 78, 26, 6, g_themeColor);
        disp->setTextFont(2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(SCREEN_W / 2, 120, "DEAL");
        gamePhase = 0;
    }
}

static void startBet() {
    disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
    drawGambleButtons();
    drawMessage("PICK  LOW  OR  HIGH  .  COLLECT  TO  KEEP", g_themeColor);
    dice = 0;
    drawCardBack(tft, PT_X + 2 * CARD_GAP, CARD_Y);
    gamePhase = 3;
}

static void collectWin(unsigned long amount) {
    if (amount <= 0) return;
    while (amount > 0) {
        uint16_t step = amount <= 100    ? 1      :
                        amount <= 1000   ? 100    :
                        amount < 10000   ? 1000   :
                        amount < 100000  ? 10000  :
                        amount < 1000000 ? 100000 : 1000000;
        amount  -= step;
        payout  -= step;
        credits += step;
        updatePayout();
        drawCredits();
        if (disp == &tft) delay(10);
    }
    saveCredits();
}

static void takeMoneyAndRun() {
    if (payout > 0) collectWin(payout);
    if (disp == &tft) delay(600);
    gamePhase = 0;
    redrawAll();
}

// ── Hand evaluation ────────────────────────────────────────────────────────

static void evaluateWin(bool doHold) {
    uint8_t numbs[14] = {0};
    uint8_t suits[4] = {0};
    uint8_t minimum = 13, maximum = 2;
    uint8_t count = 1, rank = 0;
    uint8_t pair1 = 0, pair2 = 0;

    for (int i = 0; i < 5; i++) {
        uint8_t r = cardRank(hand[i]);
        uint8_t s = cardSuit(hand[i]);
        numbs[r]++;
        if (r > 0) suits[s]++;
        if (r > 1 && r < minimum) minimum = r;
        if (r > maximum) maximum = r;
    }

    for (int i = 1; i <= 13; i++) {
        if (numbs[i] > count) { count = numbs[i]; rank = i; }
        if (numbs[(i % 13) + 1] == 2) { pair2 = pair1; pair1 = (i % 13) + 1; }
    }

    bool flush = false;
    for (int i = 0; i < 4; i++)
        if (suits[i] + numbs[0] >= 5) flush = true;

    bool straight = false;
    uint8_t jk = numbs[0];
    if (count == 1 || (count == 2 && jk >= 1) || (count == 3 && jk >= 2)) {
        if (numbs[1] == 1 && maximum <= 5) {
            uint8_t distinct = 0;
            for (int i = 1; i <= 13; i++) if (numbs[i] > 0) distinct++;
            if (distinct + jk >= 5) straight = true;
        } else if (numbs[1] == 1 && minimum >= 10) {
            uint8_t distinct = 0;
            for (int i = 10; i <= 13; i++) if (numbs[i] > 0) distinct++;
            if (numbs[1] > 0) distinct++;
            if (distinct + jk >= 5) straight = true;
        } else {
            uint8_t allDistinct = 0;
            for (int i = 1; i <= 13; i++) if (numbs[i] > 0) allDistinct++;
            if (allDistinct + jk >= 5) {
                if ((maximum - minimum) < 5 + jk) straight = true;
            }
        }
    }

    win = -1;
    if (count + numbs[0] >= 5)
        win = 0;
    else if (straight && flush && maximum >= 10 &&
             ((maximum - minimum < 5 && numbs[1] == 0) || (minimum >= 10 && numbs[1] == 1)))
        win = 1;
    else if (straight && flush)
        win = 2;
    else if (count + numbs[0] == 4)
        win = 3;
    else if ((count == 3 && pair1) || (pair1 && pair2 && numbs[0]))
        win = 4;
    else if (flush)
        win = 5;
    else if (straight)
        win = 6;
    else if (count + numbs[0] == 3)
        win = 7;
    else if (pair1 && pair2)
        win = 8;
    else if ((pair1 > 10 || pair1 == 1) ||
             (numbs[0] && (maximum > 10 || numbs[1])))
        win = 9;

    if (doHold && win >= 0) {
        for (int i = 0; i < 5; i++) {
            uint8_t r = cardRank(hand[i]);
            if ((r == pair1 || r == pair2 || r == rank || r == 0) ||
                (maximum > 10 && pair1 == 0 && r == maximum && win == 9) ||
                flush || straight) {
                hold[i] = true;
            } else if (maximum <= 10 && pair1 == 0 && r == 1 && win == 9) {
                hold[i] = true;
            }
            drawHoldFrame(tft, i, hold[i], false);
        }
    }

    payout = (win >= 0) ? (unsigned long)PAYOUTS[win] * BET : 0;
}

// ── Serial capture ─────────────────────────────────────────────────────────

static void handleSerialCapture() {
    TFT_eSprite spr(&tft);
    spr.setColorDepth(8);
    uint8_t *fb = (uint8_t *)spr.createSprite(SCREEN_W, SCREEN_H);
    if (!fb) {
        Serial.print("OOM:");
        Serial.println(ESP.getMaxAllocHeap());
        return;
    }
    auto *prev = disp;
    disp = &spr;
    redrawAll();
    disp = prev;
    Serial.print("RGB332:");
    Serial.write(fb, SCREEN_W * SCREEN_H);
    Serial.flush();
    spr.deleteSprite();
}

// ── Calibration version ─────────────────────────────────────────────────────
// Increment when calibration screens change to force re-calibration on upgrade.
#define CURRENT_CAL_VER  3   // v1.1.0: universal 2USB compatibility release

// ── ILI9341 MADCTL configuration for 2USB landscape displays ──────────────
// Different CYD boards mount the LCD glass in different orientations.  We
// combine two bits to cover all variants:
//   MV (bit 5): row/column swap — fixes 90 degree rotation on portrait-native glass
//   MY (bit 7): Y-axis mirror — fixes upside-down display
// Four combos cycle via calibration; the chosen value is stored in NVS.
static uint8_t s_madctl = 0x80;   // NVS-backed MADCTL value (default: MY)

static void applyOrientation() {
#if CYD_USB_VERSION == 2
    tft.setRotation(1);                              // landscape dimensions
    tft.writecommand(TFT_MADCTL);                    // override MADCTL set by setRotation
    tft.writedata(s_madctl);
#else
    tft.setRotation(1);
#endif
}

// Build the MADCTL value for a given combo index (0-3).
// 0: MV=1, MY=0 -> 0x28 (swap, no mirror  — portrait glass)
// 1: MV=1, MY=1 -> 0xA8 (swap + mirror)
// 2: MV=0, MY=0 -> 0x00 (no swap, no mirror — landscape glass)
// 3: MV=0, MY=1 -> 0x80 (no swap, mirror   — landscape glass flipped)
static uint8_t madctlForCombo(int idx) {
    switch (idx & 3) {
        case 0:  return TFT_MAD_MV | TFT_MAD_BGR;            // 0x28
        case 1:  return TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR; // 0xA8
        case 2:  return 0x00;                                // 0x00
        default: return TFT_MAD_MY;                          // 0x80
    }
}

// ── First-boot display calibration (2USB only) ────────────────────────────
// Cycles through the 4 MADCTL combinations so the user can find the correct
// one.  Shows a large asymmetric pattern that makes the orientation obvious.
static void displayCalibrate() {
#if CYD_USB_VERSION == 2
    if (nvsGetInt("cal_ver", -1) >= CURRENT_CAL_VER) {
        s_madctl = (uint8_t)nvsGetInt("madctl", 0x80);
        return;
    }

    s_madctl = madctlForCombo(0);  // start at combo 0
    digitalWrite(TFT_BL, HIGH);

    auto drawDisplayCal = [&]() {
        disp->fillScreen(COL_BG);
        applyOrientation();
        disp->fillScreen(COL_BG);

        // ── Distinctive corner markers ──
        // Top-left: amber filled triangle pointing down-right
        disp->fillTriangle(2, 2, 60, 2, 2, 60, COL_AMBER);
        disp->fillTriangle(4, 4, 56, 4, 4, 56, COL_BG);
        disp->fillTriangle(2, 2, 60, 2, 2, 60, COL_AMBER);

        // Top-right: colored "L" bracket
        disp->fillRect(SCREEN_W - 50, 2, 48, 8, g_themeColor);
        disp->fillRect(SCREEN_W - 8, 2, 6, 48, g_themeColor);

        // Bottom-left: amber ring
        disp->fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);
        disp->fillCircle(24, SCREEN_H - 24, 16, COL_BG);
        disp->fillCircle(24, SCREEN_H - 24, 20, COL_AMBER);

        // Bottom-right: crosshair + circle
        disp->drawLine(SCREEN_W - 40, SCREEN_H - 24, SCREEN_W - 8, SCREEN_H - 24, g_themeColor);
        disp->drawLine(SCREEN_W - 24, SCREEN_H - 40, SCREEN_W - 24, SCREEN_H - 8, g_themeColor);
        disp->drawCircle(SCREEN_W - 24, SCREEN_H - 24, 14, g_themeColor);

        // Center: "T" orientation letter
        disp->fillRect(SCREEN_W / 2 - 16, SCREEN_H / 2 - 24, 32, 6, COL_WHITE);
        disp->fillRect(SCREEN_W / 2 - 4, SCREEN_H / 2 - 24, 8, 48, COL_WHITE);

        // ── Combo number and description ──
        int idx;
        if      (s_madctl == (TFT_MAD_MV | TFT_MAD_BGR))               idx = 0;
        else if (s_madctl == (TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR))   idx = 1;
        else if (s_madctl == 0x00)                                      idx = 2;
        else                                                             idx = 3;

        disp->setTextFont(4);
        disp->setTextColor(g_themeColor, COL_BG);
        char buf[16]; snprintf(buf, sizeof(buf), "MODE %d", idx);
        int tw = disp->textWidth(buf);
        disp->setCursor((SCREEN_W - tw) / 2, 68);
        disp->print(buf);

        // Tap instruction
        disp->setTextFont(2);
        disp->setTextColor(COL_WHITE, COL_BG);
        const char *msg = "Tap to change";
        tw = disp->textWidth(msg);
        disp->setCursor((SCREEN_W - tw) / 2, SCREEN_H - 72);
        disp->print(msg);

        // Hold instruction
        disp->setTextFont(1);
        disp->setTextColor(COL_DIM_GRAY, COL_BG);
        msg = "Hold 2s to confirm";
        tw = disp->textWidth(msg);
        disp->setCursor((SCREEN_W - tw) / 2, SCREEN_H - 52);
        disp->print(msg);
    };

    drawDisplayCal();

    {
        unsigned long holdStart = 0;
        bool wasTouched = false;
        int  curCombo = 0;

        while (true) {
            bool nowTouched = touchIsHeld();

            if (nowTouched && !wasTouched) {
                holdStart = millis();
            } else if (!nowTouched && wasTouched && holdStart > 0) {
                if (millis() - holdStart < 1200) {
                    curCombo = (curCombo + 1) & 3;
                    s_madctl = madctlForCombo(curCombo);
                    drawDisplayCal();
                }
            }

            if (nowTouched && wasTouched && holdStart > 0) {
                if (millis() - holdStart >= 2000) break;
            }

            wasTouched = nowTouched;
            delay(30);
        }

        while (touchIsHeld()) { delay(30); }
        delay(200);
    }

    nvsPutInt("madctl", s_madctl);
    // cal_ver is written by touchCalibrate() below
#endif
}

// ── Boot splash ────────────────────────────────────────────────────────────

static void showSplash() {
    disp->fillScreen(COL_BG);

    // ── 4 Aces fanned across the top ──
    // A♠ (spades), A♥ (hearts), A♦ (diamonds), A♣ (clubs)
    // Arranged in a slight fan/spread
    uint8_t aces[4] = {
        1*4 + 3,  // A♠ (rank 1=A, suit 3=spades)
        1*4 + 2,  // A♥ (rank 1=A, suit 2=hearts)
        1*4 + 1,  // A♦ (rank 1=A, suit 1=diamonds)
        1*4 + 0   // A♣ (rank 1=A, suit 0=clubs)
    };

    // Spread the 4 aces with slight rotation/offset for a fan effect
    int cardW = 50, cardH = 74;
    int startX = (SCREEN_W - (4 * cardW + 3 * 8)) / 2;  // centered row with gaps
    int aceY = 8;

    for (int i = 0; i < 4; i++) {
        int cx = startX + i * (cardW + 8);
        int cy = aceY + abs(i - 1) * 6;  // slight arc: middle cards lower
        // Draw scaled-down card
        uint16_t fill = COL_BG;
        disp->fillRoundRect(cx, cy, cardW, cardH, 4, fill);
        disp->drawRoundRect(cx, cy, cardW, cardH, 4, g_themeColor);
        disp->drawRoundRect(cx + 1, cy + 1, cardW - 2, cardH - 2, 4, g_themeColor);

        // Rank
        uint8_t suit = cardSuit(aces[i]);
        uint16_t col = suitColor(suit);
        disp->setTextFont(4);
        disp->setTextColor(col, fill);
        disp->setTextDatum(MC_DATUM);
        disp->drawString("A", cx + cardW/2, cy + cardH/2 - 4);

        // Suit symbol
        drawSuitSymbol(tft, cx + cardW/2, cy + cardH/2 + 20, 16, suit);
    }

    // ── Branding ──
    int tw;
    disp->setTextFont(4);
    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXMayDayXx");
    disp->setCursor((SCREEN_W - tw) / 2, 104);
    disp->print("xXMayDayXx");

    disp->setTextFont(2);
    disp->setTextColor(COL_WHITE, COL_BG);
    tw = disp->textWidth("xXCYD-PokerXx");
    disp->setCursor((SCREEN_W - tw) / 2, 140);
    disp->print("xXCYD-PokerXx");

    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXQuantum-SmokeXx");
    disp->setCursor((SCREEN_W - tw) / 2, 162);
    disp->print("xXQuantum-SmokeXx");

    disp->setTextFont(1);
    disp->setTextColor(COL_DIM_GRAY, COL_BG);
    tw = disp->textWidth("Loading...");
    disp->setCursor((SCREEN_W - tw) / 2, 200);
    disp->print("Loading...");
}

// ── First-boot touch calibration (2USB only) ──────────────────────────────
// 2USB boards ship with digitizers in one of four orientations. Cycles
// through all four XPT2046 rotations (0-3) so the user can find the one
// where the touch cursor follows their finger correctly.
// Tap to cycle; hold 2s to confirm. Runs once (cal_ver guard).
static void touchCalibrate() {
#if CYD_USB_VERSION == 2
    if (nvsGetInt("cal_ver", -1) >= CURRENT_CAL_VER) return;

    digitalWrite(TFT_BL, HIGH);

    // ── Draw static background ──
    auto drawStatic = [&]() {
        disp->fillScreen(COL_BG);

        // Rotation number dead center
        disp->setTextFont(4);
        disp->setTextColor(g_themeColor, COL_BG);
        char buf[4]; snprintf(buf, sizeof(buf), "%d", touchGetRotation());
        int tw = disp->textWidth(buf);
        disp->setTextDatum(TC_DATUM);
        disp->drawString(buf, SCREEN_W / 2, SCREEN_H / 2 - 40);

        // Primary instruction
        disp->setTextFont(2);
        disp->setTextColor(COL_WHITE, COL_BG);
        const char *msg = "Tap to cycle touch";
        tw = disp->textWidth(msg);
        disp->setTextDatum(TC_DATUM);
        disp->drawString(msg, SCREEN_W / 2, SCREEN_H / 2);

        // Secondary instruction
        disp->setTextFont(1);
        disp->setTextColor(COL_DIM_GRAY, COL_BG);
        msg = "Hold 2s to confirm";
        tw = disp->textWidth(msg);
        disp->setTextDatum(TC_DATUM);
        disp->drawString(msg, SCREEN_W / 2, SCREEN_H / 2 + 30);

        // Four corner crosshair targets
        const int CX = 14, CY = 14, CS = 18;
        uint16_t tc = COL_DIM_GRAY;
        disp->drawRect(CX, CY, CS, CS, tc);
        disp->drawLine(CX, CY, CX + CS, CY + CS, tc);
        disp->drawLine(CX, CY + CS, CX + CS, CY, tc);

        disp->drawRect(SCREEN_W - CX - CS, CY, CS, CS, tc);
        disp->drawLine(SCREEN_W - CX - CS, CY, SCREEN_W - CX, CY + CS, tc);
        disp->drawLine(SCREEN_W - CX - CS, CY + CS, SCREEN_W - CX, CY, tc);

        disp->drawRect(CX, SCREEN_H - CY - CS, CS, CS, tc);
        disp->drawLine(CX, SCREEN_H - CY, CX + CS, SCREEN_H - CY - CS, tc);
        disp->drawLine(CX, SCREEN_H - CY - CS, CX + CS, SCREEN_H - CY, tc);

        disp->drawRect(SCREEN_W - CX - CS, SCREEN_H - CY - CS, CS, CS, tc);
        disp->drawLine(SCREEN_W - CX, SCREEN_H - CY, SCREEN_W - CX - CS, SCREEN_H - CY - CS, tc);
        disp->drawLine(SCREEN_W - CX - CS, SCREEN_H - CY, SCREEN_W - CX, SCREEN_H - CY - CS, tc);
    };

    drawStatic();

    unsigned long holdStart = 0;
    bool wasTouched = false;
    int curX = -1, curY = -1;
    int lastX = -1, lastY = -1;
    bool dirty = false;

    while (true) {
        int16_t tx, ty;
        bool nowTouched = touchIsHeld(&tx, &ty);

        if (nowTouched) {
            curX = tx; curY = ty;
        }

        // ── Touch-down ──
        if (nowTouched && !wasTouched) {
            holdStart = millis();
            lastX = curX; lastY = curY;
            dirty = true;
        }
        // ── Released ──
        else if (!nowTouched && wasTouched && holdStart > 0) {
            if (millis() - holdStart < 1200) {
                touchSetRotation((touchGetRotation() + 1) % 4);
                drawStatic();
            }
            // Erase cursor
            if (lastX >= 0) disp->fillCircle(lastX, lastY, 7, COL_BG);
            lastX = lastY = -1;
            dirty = false;
        }
        // ── Hold-to-confirm ──
        else if (nowTouched && wasTouched && holdStart > 0) {
            if (millis() - holdStart >= 2000) {
                if (lastX >= 0) disp->fillCircle(lastX, lastY, 7, COL_BG);
                break;
            }
        }

        // ── Update live cursor ──
        if (nowTouched && dirty && (curX != lastX || curY != lastY)) {
            if (lastX >= 0) disp->fillCircle(lastX, lastY, 7, COL_BG);
            disp->fillCircle(curX, curY, 6, COL_AMBER);
            disp->drawCircle(curX, curY, 6, COL_WHITE);
            lastX = curX; lastY = curY;
        }

        wasTouched = nowTouched;
        delay(30);
    }

    // Wait for finger to lift
    while (touchIsHeld()) { delay(30); }
    delay(200);

    nvsPutInt("cal_ver", CURRENT_CAL_VER);  // marks ALL calibrations complete
    nvsPutInt("touch_cal", 1);              // keep old key for backwards compat
#endif
}

// ── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(34) + micros());

    themeInit();
    loadCredits();
    holdemInit();

    tft.init();

    // ── Display rotation ─────────────────────────────────────────────────
    // 1-USB: standard landscape → rotation 1
    // 2-USB: NVS-backed LGFX rotation (calibrated on first boot)
#if CYD_USB_VERSION == 2
    s_madctl = (uint8_t)nvsGetInt("madctl", 0x80);
    applyOrientation();
#else
    tft.setRotation(1);
#endif

    disp->fillScreen(COL_BG);

    // Init touch early so calibration can use it
    touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
#if CYD_USB_VERSION == 2
    // 2USB boards have multiple touch-digitizer orientations.
    // "touch_rot" (0-3) stores the XPT2046 rotation index.
    // Migrate from old "touch_cal" / "touch_flip" boolean keys if present.
    {
        Preferences p; p.begin("cyd-poker", true);
        int rot = p.getInt("touch_rot", -1);
        if (rot < 0) {
            int oldCal = p.getInt("touch_cal", 0);
            if (!oldCal) oldCal = p.getInt("touch_flip", 1);
            rot = oldCal ? 2 : 0;
        }
        s_touchRotation = rot;
        p.end();
    }
    ts.setRotation(s_touchRotation);
#else
    ts.setRotation(0);   // 1-USB: standard touch orientation
#endif

    // First-boot calibrations — only on 2USB, only when cal_ver < CURRENT_CAL_VER
    displayCalibrate();
    applyOrientation();   // re-apply in case calibration changed it
    touchCalibrate();

    // Boot splash
    showSplash();
    delay(4000);

    // Backlight on
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    redrawAll();
    Serial.println("CYD-Poker ready");
}

// ── Loop ───────────────────────────────────────────────────────────────────

void loop() {
    // Serial capture protocol
    if (Serial.available()) {
        int cmd = Serial.read();
        if (cmd == 'R' || cmd == 'r') Serial.println("READY");
        else if (cmd == 'S' || cmd == 's') handleSerialCapture();
        else if (cmd >= '0' && cmd <= '9') redrawAll();
#if CYD_USB_VERSION == 2
        else if (cmd == 'M' || cmd == 'm') {
            int cur = 3;
            if      (s_madctl == (TFT_MAD_MV | TFT_MAD_BGR))               cur = 0;
            else if (s_madctl == (TFT_MAD_MV | TFT_MAD_MY | TFT_MAD_BGR)) cur = 1;
            else if (s_madctl == 0x00)                                     cur = 2;
            cur = (cur + 1) & 3;
            s_madctl = madctlForCombo(cur);
            nvsPutInt("madctl", s_madctl);
            nvsPutInt("cal_ver", CURRENT_CAL_VER);
            applyOrientation();
            Serial.print("MADCTL_MODE:");
            Serial.println(cur);
            redrawAll();
        }
        else if (cmd == 'T' || cmd == 't') {
            int rot = (touchGetRotation() + 1) % 4;
            touchSetRotation(rot);
            nvsPutInt("touch_cal", 1);
            Serial.print("TOUCH_ROT:");
            Serial.println(rot);
        }
#endif
    }

    readTouch();
    if (!touched) { delay(40); return; }

    // ── Power button (any phase) ────────────────────────────────────
    if (hitPowerButton()) {
        goToSleep();
        return;
    }

    // ── Hold'em mode ────────────────────────────────────────────────
    if (gameMode == 1) {
        // VIDEO POKER button works in ALL stages
        if (tx >= HM_BACK_X && tx <= HM_BACK_X + HM_BACK_W &&
            ty >= HM_BACK_Y && ty <= HM_BACK_Y + HM_BACK_H) {
            gameMode = 0;
            credits = g_hm.playerStack;
            saveCredits();
            gamePhase = 0;
            redrawAll();
            delay(300);
            return;
        }

        if (g_hm.stage == HM_IDLE) {
            int cardCX = SCREEN_W / 2 + 14;
            int dW = 80, dH = 28, dX = cardCX - dW / 2, dY = 104;
            if (tx >= dX && tx <= dX + dW && ty >= dY && ty <= dY + dH) {
                credits = g_hm.playerStack;
                if (credits < HOLDEM_BB) {
                    g_hm.playerStack = HOLDEM_STARTING_STACK;
                    credits = HOLDEM_STARTING_STACK;
                }
                holdemNewHand();
                drawHoldemScreen();
                delay(300);
                return;
            }
        } else if (g_hm.stage >= HM_PREFLOP && g_hm.stage <= HM_RIVER && !g_hm.playerFolded) {
            int plyY = 186;
            int cardCX = SCREEN_W / 2 + 14;
            int pc1x = cardCX - HCARD_W - 3;
            int pc2x = cardCX + 3;
            int btnH = HCARD_H - 6;
            int btnY = plyY + 2;
            int foldX = pc1x - HM_SIDE_BTN_W - 8;
            int rightX = pc2x + HCARD_W + 8;
            int rw = HM_SIDE_BTN_W + 8;
            int rh = (btnH - 4) / 2;
            int raiseY = btnY + rh + 4;

            // FOLD (top half of left stack)
            if (tx >= foldX && tx <= foldX + HM_SIDE_BTN_W &&
                ty >= btnY && ty <= btnY + rh) {
                holdemPlayerAction(0);
                credits = g_hm.playerStack; saveCredits();
                drawHoldemScreen(); delay(400); return;
            }
            // RESET (bottom half of left stack)
            if (tx >= foldX && tx <= foldX + HM_SIDE_BTN_W &&
                ty >= raiseY && ty <= raiseY + rh) {
                credits = STARTING_CREDITS;
                g_hm.playerStack = HOLDEM_STARTING_STACK;
                g_hm.aiStack = HOLDEM_STARTING_STACK;
                g_hm.stage = HM_IDLE;
                saveCredits();
                redrawAll(); delay(400); return;
            }
            if (tx >= rightX && tx <= rightX + rw &&
                ty >= btnY && ty <= btnY + rh) {
                holdemPlayerAction(1);
                credits = g_hm.playerStack; saveCredits();
                drawHoldemScreen(); delay(400); return;
            }
            if (tx >= rightX && tx <= rightX + rw &&
                ty >= raiseY && ty <= raiseY + rh) {
                holdemPlayerAction(2);
                credits = g_hm.playerStack; saveCredits();
                drawHoldemScreen(); delay(400); return;
            }
        } else if (g_hm.stage == HM_HAND_OVER) {
            int plyY = 186;
            int cardCX = SCREEN_W / 2 + 14;
            int btnW = HCARD_W * 2 + 6, btnH = 24;
            int btnX = cardCX - btnW/2, btnY = plyY + HCARD_H + 4;
            if (btnY > 214) btnY = 214;
            if (tx >= btnX && tx <= btnX + btnW && ty >= btnY && ty <= btnY + btnH) {
                g_hm.playerDealer = !g_hm.playerDealer;
                holdemNewHand();
                credits = g_hm.playerStack;
                saveCredits();
                drawHoldemScreen();
                delay(300);
                return;
            }
        }
        delay(120);
        return;
    }

    // ── Theme cycle: tap theme name in credits area ─────────────────
    if (gamePhase == 0 && tx > RIGHT_X && ty < 30) {
        themeNext();
        showThemeName();
        redrawAll();
        delay(300);
        return;
    }

    // ── Phase 1: Hold ───────────────────────────────────────────────
    if (gamePhase == 1) {
        for (int i = 0; i < 5; i++) {
            int cx = PAYTABLE_X + i * CARD_GAP;
            if (tx > cx - 5 && tx < cx + CARD_W + 5 && ty > CARD_Y - 10) {
                hold[i] = !hold[i];
                drawCardFace(tft, cx, CARD_Y, hand[i]);
                drawHoldFrame(tft, i, hold[i], false);
                delay(200);
                break;
            }
        }
        if (hitActionButton()) { finalDraw(); delay(300); return; }
    }

    // ── Phase 2: Collect or Double ──────────────────────────────────
    if (gamePhase == 2) {
        if (hitActionButton()) { takeMoneyAndRun(); delay(300); return; }
        if (tx < RIGHT_X && ty < MSG_Y) { startBet(); delay(300); return; }
    }

    // ── Phase 3: Gamble ────────────────────────────────────────────
    if (gamePhase == 3) {
        int gIdx = hitGambleButton();
        if (gIdx == 0) {
            collectWin(payout / 2);
            delay(300);
            return;
        } else if ((gIdx == 1 || gIdx == 2) && payout < 500000000) {
            uint8_t h = (gIdx == 2) ? 1 : 0;
            uint8_t r = random(2);
            payout = (1 - r) * payout * 2;
            int dx = 6 + 11 * (dice % 14) + 2 * (dice / 14);
            int dy = CARD_Y + 2 * (dice / 14);
            uint8_t card = 4 + random(24) + 28 * (r ^ h);
            drawCardFace(tft, dx, dy, card);
            delay(400);
            dice++;
            if (r == 1) {
                takeMoneyAndRun();
            } else {
                updatePayout();
                drawCardBack(tft, dx, dy);
            }
            delay(200);
            return;
        }
    }

    // ── Phase 0 / 4: Deal or New Game ───────────────────────────────
    if (gamePhase == 0 || gamePhase == 4) {
        // Centered DEAL/NEW GAME button below paytable
        int dealW = 80, dealH = 28;
        int dealX = SCREEN_W / 2 - dealW / 2;
        int dealY = 102;
        bool hitDeal = (tx >= dealX && tx <= dealX + dealW &&
                        ty >= dealY && ty <= dealY + dealH);
        if (hitDeal || (gamePhase == 4 && hitActionButton())) {
            if (gamePhase == 4) { credits += payout; payout = 0; saveCredits(); }
            startDeal();
            delay(300);
            return;
        }

        // Mode toggle — only after DEAL check (DEAL takes priority)
        if (gamePhase == 0 && hitModeToggle()) {
            gameMode = 1;
            g_hm.playerStack = credits;
            g_hm.stage = HM_IDLE;
            redrawAll();
            delay(300);
            return;
        }
    }

    delay(120);
}
