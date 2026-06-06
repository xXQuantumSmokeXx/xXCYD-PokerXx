#pragma once
// Display type and macros (TC_DATUM etc.) are needed even for template parsing.
// holdem.cpp includes this header without a display backend — pull one in.
#ifdef USE_LOVYAN_GFX
#include "lgfx_cyd.h"
#else
#include <TFT_eSPI.h>
#endif
#include "theme.h"

static const char* RANK_NAMES[] = {"J", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

inline uint8_t cardRank(uint8_t c)   { return c / 4; }
inline uint8_t cardSuit(uint8_t c)   { return c % 4; }
inline uint16_t suitColor(uint8_t s) { return g_themeColor; }

inline const char* rankStr(uint8_t rank) {
    if (rank <= 13) return RANK_NAMES[rank];
    return "?";
}

// ── Suit symbols ──────────────────────────────────────────────────────────

template<typename Gfx>
static void drawHeart(Gfx &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 3 / 10; if (r < 3) r = 3;
    tft.fillCircle(cx - r + 1, cy - r + 2, r, color);
    tft.fillCircle(cx + r - 1, cy - r + 2, r, color);
    tft.fillRect(cx - r, cy - r * 2 + 2, r * 2, r * 2, color);
    int halfW = r * 2;
    tft.fillTriangle(cx - halfW, cy - r + 2, cx + halfW, cy - r + 2, cx, cy + sz/2 + 1, color);
}

template<typename Gfx>
static void drawDiamond(Gfx &tft, int cx, int cy, int sz, uint16_t color) {
    tft.fillTriangle(cx, cy - sz/2, cx + sz/2, cy, cx, cy + sz/2, color);
    tft.fillTriangle(cx, cy - sz/2, cx - sz/2, cy, cx, cy + sz/2, color);
}

template<typename Gfx>
static void drawClub(Gfx &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 2 / 9; if (r < 2) r = 2;
    tft.fillCircle(cx, cy - r, r + 1, color);
    tft.fillCircle(cx - r, cy + r - 1, r, color);
    tft.fillCircle(cx + r, cy + r - 1, r, color);
    tft.fillRect(cx - 2, cy + r + 1, 4, sz/2, color);
}

template<typename Gfx>
static void drawSpade(Gfx &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 3 / 10; if (r < 3) r = 3;
    tft.fillTriangle(cx, cy - sz/2, cx - r + 1, cy - r, cx + r - 1, cy - r, color);
    tft.fillCircle(cx - r, cy + 1, r, color);
    tft.fillCircle(cx + r, cy + 1, r, color);
    tft.fillRect(cx - r, cy - r, r * 2, r + 2, color);
    tft.fillRect(cx - 2, cy + r + 1, 4, sz/2 + 1, color);
}

template<typename Gfx>
static void drawSuitSymbol(Gfx &tft, int cx, int cy, int sz, uint8_t suit) {
    uint16_t col = g_themeColor;
    switch (suit) {
        case 0: drawClub(tft, cx, cy, sz, col);    break;
        case 1: drawDiamond(tft, cx, cy, sz, col);  break;
        case 2: drawHeart(tft, cx, cy, sz, col);    break;
        case 3: drawSpade(tft, cx, cy, sz, col);    break;
    }
}

template<typename Gfx>
static void drawStar(Gfx &tft, int cx, int cy, int sz, uint16_t color) {
    int rOuter = sz / 2, rInner = sz / 5;
    int pts[10][2];
    for (int i = 0; i < 5; i++) {
        float aOuter = (i * 72.0f - 90.0f) * PI / 180.0f;
        float aInner = ((i * 72.0f) + 36.0f - 90.0f) * PI / 180.0f;
        pts[i*2][0]=cx+(int)(rOuter*cosf(aOuter)); pts[i*2][1]=cy+(int)(rOuter*sinf(aOuter));
        pts[i*2+1][0]=cx+(int)(rInner*cosf(aInner)); pts[i*2+1][1]=cy+(int)(rInner*sinf(aInner));
    }
    for (int i = 0; i < 10; i++) {
        int j = (i+1)%10;
        tft.fillTriangle(cx, cy, pts[i][0], pts[i][1], pts[j][0], pts[j][1], color);
    }
}

// ── Card face (black fill, themed outline + symbols) ─────────────────────

template<typename Gfx>
static void drawCardFace(Gfx &tft, int x, int y, uint8_t card) {
    uint8_t rank = cardRank(card);
    uint8_t suit = cardSuit(card);

    tft.fillRect(x + 1, y + 1, CARD_W - 1, CARD_H - 1, COL_BG);
    tft.drawRoundRect(x, y, CARD_W, CARD_H, 5, g_themeColor);
    tft.drawRoundRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, 5, g_themeColor);

    if (rank == 0) {
        drawStar(tft, x + CARD_W/2, y + CARD_H/2 - 6, 22, g_themeColor);
        tft.setTextFont(2); tft.setTextColor(g_themeColor, COL_BG);
        tft.setTextDatum(TC_DATUM);
        tft.drawString("JOKER", x + CARD_W/2, y + CARD_H - 14);
        return;
    }

    tft.setTextFont(2); tft.setTextColor(g_themeColor, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(rankStr(rank), x + 5, y + 3);
    drawSuitSymbol(tft, x + CARD_W/2, y + CARD_H/2 + 3, 20, suit);
    tft.setTextDatum(BR_DATUM);
    tft.drawString(rankStr(rank), x + CARD_W - 5, y + CARD_H - 3);
}

// ── Card back ─────────────────────────────────────────────────────────────

template<typename Gfx>
static void drawCardBack(Gfx &tft, int x, int y) {
    tft.fillRect(x + 1, y + 1, CARD_W - 1, CARD_H - 1, COL_BG);
    tft.drawRoundRect(x, y, CARD_W, CARD_H, 5, g_themeColor);
    tft.drawRoundRect(x + 2, y + 2, CARD_W - 4, CARD_H - 4, 4, g_themeColor);
    for (int cy = y + 6; cy < y + CARD_H - 6; cy += 6)
        for (int cx = x + 6; cx < x + CARD_W - 6; cx += 6)
            tft.fillRect(cx, cy, 2, 2, (g_themeColor >> 1) & 0x7BEF);
    int cx = x + CARD_W/2, cy = y + CARD_H/2;
    tft.fillTriangle(cx, cy - 10, cx + 8, cy, cx, cy + 10, g_themeColor);
    tft.fillTriangle(cx, cy - 10, cx - 8, cy, cx, cy + 10, g_themeColor);
}

// ── Hold frame ────────────────────────────────────────────────────────────

template<typename Gfx>
static void drawHoldFrame(Gfx &tft, int i, bool held, bool clear) {
    int x = PAYTABLE_X + i * CARD_GAP;
    int fx = x - 2, fy = CARD_Y - 2, fw = CARD_W + 4, fh = CARD_H + 14;
    if (held) {
        tft.drawRoundRect(fx, fy, fw, fh, 6, g_themeColor);
        tft.drawRoundRect(fx + 1, fy + 1, fw - 2, fh - 2, 6, g_themeColor);
    }
    int lx = x + CARD_W / 2, ly = fy + fh - 6;
    tft.setTextFont(1); tft.setTextDatum(TC_DATUM);
    tft.fillRect(lx - 24, ly - 2, 48, 12, COL_BG);
    if (held) { tft.setTextColor(g_themeColor, COL_BG); tft.drawString("HELD", lx, ly); }
}
