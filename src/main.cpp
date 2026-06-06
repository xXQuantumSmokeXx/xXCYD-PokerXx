/**
 * CYD-Poker — 5-card Draw Video Poker with Joker
 * Outlined theme-colored cards, visible buttons, theming system
 */

#include <Arduino.h>

#ifdef USE_LOVYAN_GFX
  #include "lgfx_cyd.h"
  typedef LGFX_CYD          GfxDisplay;
  typedef lgfx::LGFX_Sprite GfxSprite;
  static LGFX_CYD             tft;
  static lgfx::LovyanGFX     *disp = &tft;  // LGFX_Base* — shared by Device & Sprite
#else
  #include <TFT_eSPI.h>
  typedef TFT_eSPI    GfxDisplay;
  typedef TFT_eSprite GfxSprite;
  static TFT_eSPI     tft;
  static TFT_eSPI    *disp = &tft;
#endif

// setTextFont compatibility — LGFX base class lacks setTextFont(uint8_t),
// so we route through this helper in both builds for consistency.
#ifdef USE_LOVYAN_GFX
  static void dispSetFont(lgfx::LovyanGFX *d, uint8_t n) {
      static const lgfx::IFont* const tbl[] = {
          &lgfx::fonts::Font0, &lgfx::fonts::Font0,  // 0,1 → GLCD
          &lgfx::fonts::Font2, nullptr,                // 2 → 16 px
          &lgfx::fonts::Font4, nullptr, nullptr, nullptr,
      };
      if (n < 8 && tbl[n]) d->setFont(tbl[n]);
  }
#else
  static void dispSetFont(TFT_eSPI *d, uint8_t n) { d->setTextFont(n); }
#endif

#include <SPI.h>
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


// ── Touch ──────────────────────────────────────────────────────────────────

static bool readTouch() {
    bool nowTouched = ts.touched();
    if (!nowTouched) { wasTouched = false; touched = false; return false; }
    TS_Point p = ts.getPoint();
    tx = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W - 1);
    ty = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H - 1, 0);
    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);
    touched = true;
    if (wasTouched) return false;
    wasTouched = true;
    return true;
}

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
    dispSetFont(disp,2);
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
    dispSetFont(disp,1);

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
    dispSetFont(disp,1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(TC_DATUM);
    disp->drawString("CREDITS", CREDITS_CX, 4);
    disp->drawString(g_themes[g_themeIdx].name, CREDITS_CX, 14);
    dispSetFont(disp,4);
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
    dispSetFont(disp,2);
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
        dispSetFont(disp,2);
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
    dispSetFont(disp,2);
    disp->setTextColor(col, COL_BG);
    drawCenterText(SCREEN_W / 2, MSG_Y + MSG_H / 2, msg);
}

// ── Win box ────────────────────────────────────────────────────────────────

static void clearWinBox() {
    disp->fillRect(PT_X, PT_Y, PT_W, PT_H, COL_WIN_BG);
    disp->drawRect(PT_X, PT_Y, PT_W, PT_H, g_themeColor);
}

static void updatePayout() {
    dispSetFont(disp,4);
    disp->setTextColor(COL_WHITE, COL_WIN_BG);
    disp->setTextDatum(MC_DATUM);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", payout);
    disp->drawString(buf, PT_X + PT_W / 2, PT_Y + PT_H / 2 + 6);
}

static void highlightWin(uint16_t col) {
    if (win < 0) return;
    int rowY = PT_Y + win * PT_LINE_H;
    dispSetFont(disp,1);
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
    dispSetFont(disp,1);
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
        dispSetFont(disp,1);
        disp->setTextColor(g_themeColor, fill);
        disp->setTextDatum(MC_DATUM);
        disp->drawString("J", x + HCARD_W/2, y + HCARD_H/2);
        return;
    }

    // Corner rank (top-left)
    dispSetFont(disp,1);
    disp->setTextColor(col, fill);
    disp->setTextDatum(TL_DATUM);
    disp->drawString(rankStr(rank), x + 3, y + 2);

    // Corner rank (bottom-right)
    disp->setTextDatum(BR_DATUM);
    disp->drawString(rankStr(rank), x + HCARD_W - 3, y + HCARD_H - 2);

    // Center suit
    drawSuitSymbol(*disp, x + HCARD_W/2, y + HCARD_H/2 + 2, 12, suit);
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

    // ── Mode toggle (top-left corner) ──
    {
        const char* mlabel = "5-CARD DRAW";
        int mw = 76, mh = 14, mx = 4, my = 2;
        disp->fillRoundRect(mx, my, mw, mh, 3, COL_BG);
        disp->drawRoundRect(mx, my, mw, mh, 3, g_themeColor);
        dispSetFont(disp,1);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(mx + mw / 2, my + mh / 2, mlabel);
    }

    // ── Back to Video Poker button (top-left) ──
    {
        int mw = 80, mh = 18, mx = 4, my = 1;
        disp->fillRoundRect(mx, my, mw, mh, 4, COL_BG);
        disp->drawRoundRect(mx, my, mw, mh, 4, g_themeColor);
        disp->drawRoundRect(mx + 1, my + 1, mw - 2, mh - 2, 4, g_themeColor);
        dispSetFont(disp,1);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(mx + mw / 2, my + mh / 2, "VIDEO POKER");
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
        dispSetFont(disp,1);
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

        // Last action (show during play AND hand over)
        if (g_hm.lastAction[0]) {
            disp->drawString(g_hm.lastAction, 4, aiY + 48);
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
    dispSetFont(disp,1);
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
            dispSetFont(disp,1);
            disp->setTextColor(col, fill);
            disp->setTextDatum(TL_DATUM);
            disp->drawString(rankStr(rank), cx + 3, commY + 3);
            disp->setTextDatum(BR_DATUM);
            disp->drawString(rankStr(rank), cx + HCCARD_W - 3, commY + HCCARD_H - 3);
            drawSuitSymbol(*disp, cx + HCCARD_W/2, commY + HCCARD_H/2 + 2, 14, suit);
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
    dispSetFont(disp,1);
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
    dispSetFont(disp,1);
    disp->setTextDatum(TL_DATUM);

    disp->setTextColor(g_themeColor, COL_BG);
    snprintf(buf, sizeof(buf), "YOU:%lu", g_hm.playerStack);
    disp->drawString(buf, 4, plyY + 6);

    if (g_hm.playerBet > 0) {
        disp->setTextColor(COL_LIGHT_GRAY, COL_BG);
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
        dispSetFont(disp,2);
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
        dispSetFont(disp,2);
        disp->setTextColor(g_themeColor, COL_BG);
        disp->setTextDatum(MC_DATUM);
        disp->drawString("FOLD", foldX + HM_SIDE_BTN_W / 2, plyY + 2 + rh / 2);

        // RESET
        int resetY = plyY + 2 + rh + 4;
        disp->fillRoundRect(foldX, resetY, HM_SIDE_BTN_W, rh, 4, COL_BG);
        disp->drawRoundRect(foldX, resetY, HM_SIDE_BTN_W, rh, 4, g_themeColor);
        disp->drawRoundRect(foldX + 1, resetY + 1, HM_SIDE_BTN_W - 2, rh - 2, 4, g_themeColor);
        dispSetFont(disp,2);
        disp->setTextColor(g_themeColor, COL_BG);
        disp->drawString("RESET", foldX + HM_SIDE_BTN_W / 2, resetY + rh / 2);

        // CALL+RAISE — right of player cards, stacked
        int rightX = pc2x + HCARD_W + 8;
        int rw = HM_SIDE_BTN_W + 8;

        disp->fillRoundRect(rightX, plyY + 2, rw, rh, 4, COL_BG);
        disp->drawRoundRect(rightX, plyY + 2, rw, rh, 4, g_themeColor);
        disp->drawRoundRect(rightX + 1, plyY + 3, rw - 2, rh - 2, 4, g_themeColor);
        dispSetFont(disp,2);
        disp->setTextColor(g_themeColor, COL_BG);
        disp->drawString(callLabel, rightX + rw / 2, plyY + 2 + rh / 2);

        int raiseY = plyY + 2 + rh + 4;
        disp->fillRoundRect(rightX, raiseY, rw, rh, 4, COL_BG);
        disp->drawRoundRect(rightX, raiseY, rw, rh, 4, g_themeColor);
        disp->drawRoundRect(rightX + 1, raiseY + 1, rw - 2, rh - 2, 4, g_themeColor);
        disp->setTextColor(g_themeColor, COL_BG);
        disp->drawString("RAISE", rightX + rw / 2, raiseY + rh / 2);

    } else if (g_hm.stage == HM_HAND_OVER) {
        int btnW = HCARD_W * 2 + 6, btnH = 24;
        int btnX = cardCX - btnW / 2;
        int btnY = plyY + HCARD_H + 4;
        if (btnY > 214) btnY = 214;
        disp->fillRoundRect(btnX, btnY, btnW, btnH, 6, COL_BG);
        disp->drawRoundRect(btnX, btnY, btnW, btnH, 6, g_themeColor);
        disp->drawRoundRect(btnX + 1, btnY + 1, btnW - 2, btnH - 2, 6, g_themeColor);
        dispSetFont(disp,2);
        disp->setTextColor(g_themeColor, COL_BG);
        drawCenterText(btnX + btnW / 2, btnY + btnH / 2, "NEXT HAND");

        // Result text — clear gap, draw centered
        if (g_hm.lastAction[0]) {
            int gapTop = commY + HCCARD_H;
            int gapH = plyY - gapTop;
            disp->fillRect(0, gapTop, SCREEN_W, gapH, COL_BG);
            dispSetFont(disp,2);
            disp->setTextColor(g_themeColor, COL_BG);
            disp->setTextDatum(MC_DATUM);
            disp->drawString(g_hm.lastAction, cardCX, gapTop + gapH / 2);
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
        dispSetFont(disp,2);
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
            drawCardFace(*disp, cx, CARD_Y, hand[i]);
            drawHoldFrame(*disp, i, hold[i], false);
        }

    } else if (gamePhase == 2) {
        drawActionButton("COLLECT");
        clearWinBox();
        dispSetFont(disp,2);
        disp->setTextColor(COL_WHITE, COL_WIN_BG);
        drawCenterText(PT_X + PT_W / 2, PT_Y + PT_H / 3, WIN_NAMES[win]);
        updatePayout();
        drawMessage("COLLECT   OR   TAP  SCORE  TO  DOUBLE", g_themeColor);

    } else if (gamePhase == 3) {
        drawGambleButtons();
        drawMessage("PICK  LOW  OR  HIGH  .  COLLECT  TO  KEEP", g_themeColor);
        disp->fillRect(0, CARD_Y - 2, SCREEN_W, CARD_H + 20, COL_BG);
        drawCardBack(*disp, PT_X + 2 * CARD_GAP, CARD_Y);

    } else if (gamePhase == 4) {
        clearWinBox();
        dispSetFont(disp,2);
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
    dispSetFont(disp,2);
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
        if (!hold[i]) drawCardBack(*disp, cx, CARD_Y);
        drawHoldFrame(*disp, i, hold[i], true);
    }
    for (int i = 0; i < 5; i++) {
        int cx = PAYTABLE_X + i * CARD_GAP;
        drawCardFace(*disp, cx, CARD_Y, hand[i]);
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
        dispSetFont(disp,2);
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
        dispSetFont(disp,2);
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
    drawCardBack(*disp, PT_X + 2 * CARD_GAP, CARD_Y);
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
            drawHoldFrame(*disp, i, hold[i], false);
        }
    }

    payout = (win >= 0) ? (unsigned long)PAYOUTS[win] * BET : 0;
}

// ── Serial capture ─────────────────────────────────────────────────────────

static void handleSerialCapture() {
    GfxSprite spr(&tft);
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

// ── Orientation test (2-USB only) ────────────────────────────────────────────

struct RotationMode {
    const char* name;
    void (*apply)();
};
static int g_rotMode = 0;  // default to standard landscape

#ifdef USE_LOVYAN_GFX
  // LovyanGFX natively supports rotations 0-7 — all mirror / flip combos.
  static void applyRotationN(int n) { tft.setRotation(n); }

  static const RotationMode ROT_MODES[] = {
      {"Rot 0 Portrait",            []{ applyRotationN(0); }},
      {"Rot 1 Landscape",           []{ applyRotationN(1); }},
      {"Rot 2 Portrait 180",        []{ applyRotationN(2); }},
      {"Rot 3 Landscape 180",       []{ applyRotationN(3); }},
      {"Rot 4 Portrait Mirror",     []{ applyRotationN(4); }},
      {"Rot 5 Landscape Mirror",    []{ applyRotationN(5); }},
      {"Rot 6 Portrait 180 Mirr",   []{ applyRotationN(6); }},
      {"Rot 7 Landscape 180 Mirr",  []{ applyRotationN(7); }},
  };
  static const int ROT_MODE_COUNT = sizeof(ROT_MODES) / sizeof(ROT_MODES[0]);
#else
  static void applyRotation0() { tft.setRotation(1); }
  static void applyRotation1() { tft.setRotation(3); }
  static void applyRotation2() {
      tft.setRotation(1);
      tft.writecommand(TFT_MADCTL);
      tft.writedata(TFT_MAD_MX | TFT_MAD_BGR);
  }
  static void applyRotation3() {
      tft.setRotation(1);
      tft.writecommand(TFT_MADCTL);
      tft.writedata(TFT_MAD_MY | TFT_MAD_BGR);
  }
  static void applyRotation4() {
      tft.setRotation(1);
      tft.writecommand(TFT_MADCTL);
      tft.writedata(TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_BGR);
  }

  static const RotationMode ROT_MODES[] = {
      {"Standard Landscape",     applyRotation0},
      {"Landscape 180",          applyRotation1},
      {"Landscape + Mirror X",   applyRotation2},
      {"Landscape + Mirror Y",   applyRotation3},
      {"Landscape + Manual 180", applyRotation4},
  };
  static const int ROT_MODE_COUNT = sizeof(ROT_MODES) / sizeof(ROT_MODES[0]);
#endif

// Button: bottom-center, full width, tap to cycle
#define ROT_BTN_X  60
#define ROT_BTN_Y  190
#define ROT_BTN_W  200
#define ROT_BTN_H  40

static bool hitRotateButton() {
    return (tx >= ROT_BTN_X && tx <= ROT_BTN_X + ROT_BTN_W &&
            ty >= ROT_BTN_Y && ty <= ROT_BTN_Y + ROT_BTN_H);
}

static void drawOrientationUI() {
    disp->fillScreen(COL_BG);
    dispSetFont(disp,2);

    // Corner labels
    disp->setTextColor(COL_WHITE, COL_BG);
    disp->setTextDatum(TL_DATUM);  disp->drawString("NW", 2, 2);
    disp->setTextDatum(TR_DATUM);  disp->drawString("NE", SCREEN_W - 2, 2);
    disp->setTextDatum(BL_DATUM);  disp->drawString("SW", 2, SCREEN_H - 2);
    disp->setTextDatum(BR_DATUM);  disp->drawString("SE", SCREEN_W - 2, SCREEN_H - 2);

    // Center crosshair
    disp->drawLine(SCREEN_W/2 - 10, SCREEN_H/2, SCREEN_W/2 + 10, SCREEN_H/2, COL_WHITE);
    disp->drawLine(SCREEN_W/2, SCREEN_H/2 - 10, SCREEN_W/2, SCREEN_H/2 + 10, COL_WHITE);

    // Current mode — above button
    disp->setTextDatum(BC_DATUM);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->drawString(ROT_MODES[g_rotMode].name, SCREEN_W/2, ROT_BTN_Y - 10);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", g_rotMode + 1, ROT_MODE_COUNT);
    dispSetFont(disp,1);
    disp->drawString(buf, SCREEN_W/2, ROT_BTN_Y - 24);

    // Tap button
    disp->fillRoundRect(ROT_BTN_X, ROT_BTN_Y, ROT_BTN_W, ROT_BTN_H, 6, COL_BG);
    disp->drawRoundRect(ROT_BTN_X, ROT_BTN_Y, ROT_BTN_W, ROT_BTN_H, 6, g_themeColor);
    disp->drawRoundRect(ROT_BTN_X+1, ROT_BTN_Y+1, ROT_BTN_W-2, ROT_BTN_H-2, 6, g_themeColor);
    dispSetFont(disp,2);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    disp->drawString("TAP TO ROTATE", SCREEN_W/2, ROT_BTN_Y + ROT_BTN_H/2);

    // Confirm area — top-right corner
    dispSetFont(disp,1);
    disp->setTextColor(COL_DIM_GRAY, COL_BG);
    disp->setTextDatum(TR_DATUM);
    disp->drawString("hold 2s to confirm", SCREEN_W - 2, 2);
}

// Quick touch read for the orientation test (touch already initialized)
static bool readTouchQuick() {
    if (!ts.touched()) return false;
    TS_Point p = ts.getPoint();
    tx = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W - 1);
    ty = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H - 1, 0);
    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);
    return true;
}

static void runOrientationTest() {
    g_rotMode = 0;  // start with standard landscape
    ROT_MODES[g_rotMode].apply();
    drawOrientationUI();

    unsigned long lastTap = millis();
    unsigned long holdStart = 0;
    bool wasTouching = false;

    while (true) {
        bool isTouching = readTouchQuick();

        // Detect tap (touch release)
        if (!isTouching && wasTouching) {
            if (millis() - holdStart < 1000) {
                // Short tap — cycle to next mode
                g_rotMode = (g_rotMode + 1) % ROT_MODE_COUNT;
                ROT_MODES[g_rotMode].apply();
                drawOrientationUI();
                lastTap = millis();
            }
            holdStart = 0;
        }

        // Detect hold start
        if (isTouching && !wasTouching) {
            holdStart = millis();
        }

        // Detect long hold (2 seconds) — confirm
        if (isTouching && holdStart > 0 && millis() - holdStart >= 2000) {
            // Visual feedback
            disp->fillScreen(COL_BG);
            dispSetFont(disp,2);
            disp->setTextColor(g_themeColor, COL_BG);
            disp->setTextDatum(MC_DATUM);
            disp->drawString("CONFIRMED", SCREEN_W/2, SCREEN_H/2 - 10);
            dispSetFont(disp,1);
            disp->drawString(ROT_MODES[g_rotMode].name, SCREEN_W/2, SCREEN_H/2 + 12);
            delay(1000);
            break;
        }

        // Auto-confirm after 30 seconds of no taps
        if (millis() - lastTap > 30000) break;

        wasTouching = isTouching;
        delay(20);
    }
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
        dispSetFont(disp,4);
        disp->setTextColor(col, fill);
        disp->setTextDatum(MC_DATUM);
        disp->drawString("A", cx + cardW/2, cy + cardH/2 - 4);

        // Suit symbol
        drawSuitSymbol(*disp, cx + cardW/2, cy + cardH/2 + 20, 16, suit);
    }

    // ── Branding ──
    int tw;
    dispSetFont(disp,4);
    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXMayDayXx");
    disp->setCursor((SCREEN_W - tw) / 2, 104);
    disp->print("xXMayDayXx");

    dispSetFont(disp,2);
    disp->setTextColor(COL_WHITE, COL_BG);
    tw = disp->textWidth("xXCYD-PokerXx");
    disp->setCursor((SCREEN_W - tw) / 2, 140);
    disp->print("xXCYD-PokerXx");

    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXQuantum-SmokeXx");
    disp->setCursor((SCREEN_W - tw) / 2, 162);
    disp->print("xXQuantum-SmokeXx");

    dispSetFont(disp,1);
    disp->setTextColor(COL_DIM_GRAY, COL_BG);
    tw = disp->textWidth("Loading...");
    disp->setCursor((SCREEN_W - tw) / 2, 200);
    disp->print("Loading...");
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
    // CYD 2.8" has two hardware revisions:
    //   1-USB (1 USB port):  standard orientation → rotation 1
    //   2-USB (2 USB ports): LCD physically flipped 180° → rotation 3
    //
    // Rotation 3 uses MADCTL = MX|MY|MV|BGR (0xE8) — full 180° landscape
    // without any manual register override, so TFT_eSPI internals stay
    // consistent with the hardware orientation.
    //
    // If the display still looks wrong, watch the corner labels ("NW" "NE"
    // "SW" "SE") during the 3-second orientation check before the splash:
    //   - Labels upside down → wrong rotation (try the other one)
    //   - Labels mirrored   → need MX flip only
    //   - Labels upside down + mirrored → need MY flip only
#if CYD_USB_VERSION == 2
    tft.setRotation(3);       // 2-USB: full 180° landscape
    tft.invertDisplay(true);  // 2-USB: colors are inverted on this variant
#else
    tft.setRotation(1);       // 1-USB: standard landscape
#endif

    tft.fillScreen(COL_BG);

    // Init touch early so the orientation test can use it
    touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
#if CYD_USB_VERSION == 2
    ts.setRotation(2);   // 2-USB: touch panel also physically 180° rotated
#else
    ts.setRotation(0);   // 1-USB: standard touch orientation
#endif

#if CYD_USB_VERSION == 2
    // ── Orientation test — tap to cycle ──────────────────────────────────
    // Tap the "TAP TO ROTATE" button to cycle through 5 rotation modes.
    // Hold anywhere for 2 seconds to confirm. Auto-confirms after 30s.
    runOrientationTest();
#endif

    // Boot splash
    showSplash();
    delay(4000);

#ifndef USE_LOVYAN_GFX
    // Backlight — LGFX handles this via its PWM light instance
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
#endif

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
        if (tx >= 4 && tx <= 84 && ty >= 1 && ty <= 19) {
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
