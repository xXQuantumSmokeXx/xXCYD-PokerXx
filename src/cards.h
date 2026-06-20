#pragma once
#include <TFT_eSPI.h>
#include "theme.h"

static const char* RANK_NAMES[] = {"J", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

inline uint8_t cardRank(uint8_t c)   { return c / 4; }
inline uint8_t cardSuit(uint8_t c)   { return c % 4; }
inline uint16_t suitColor(uint8_t s) { return g_themeColor; }

inline const char* rankStr(uint8_t rank) {
    if (rank <= 13) return RANK_NAMES[rank];
    return "?";
}

// ── Suit symbols — bold outlined (double-stroke) for thick lines, clear shape ─
static void drawHeart(TFT_eSPI &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 3 / 10; if (r < 3) r = 3;
    int halfW = r * 2;
    for (int o = 0; o < 2; o++) {
        int ox = o, oy = o;
        tft.drawCircle(cx - r + 1 + ox, cy - r + 2 + oy, r, color);
        tft.drawCircle(cx + r - 1 + ox, cy - r + 2 + oy, r, color);
        tft.drawLine(cx - halfW + ox, cy - r + 2 + oy, cx + ox, cy + sz/2 + 1 + oy, color);
        tft.drawLine(cx + halfW + ox, cy - r + 2 + oy, cx + ox, cy + sz/2 + 1 + oy, color);
    }
}
static void drawDiamond(TFT_eSPI &tft, int cx, int cy, int sz, uint16_t color) {
    int half = sz / 2;
    for (int o = 0; o < 2; o++) {
        int ox = o, oy = o;
        tft.drawLine(cx + ox, cy - half + oy, cx + half + ox, cy + oy, color);
        tft.drawLine(cx + half + ox, cy + oy, cx + ox, cy + half + oy, color);
        tft.drawLine(cx + ox, cy + half + oy, cx - half + ox, cy + oy, color);
        tft.drawLine(cx - half + ox, cy + oy, cx + ox, cy - half + oy, color);
    }
}
static void drawClub(TFT_eSPI &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 2 / 9; if (r < 2) r = 2;
    int stemBot = cy + r + 1 + sz/3;
    for (int o = 0; o < 2; o++) {
        int ox = o, oy = o;
        tft.drawCircle(cx + ox, cy - r + oy, r + 1, color);
        tft.drawCircle(cx - r - 1 + ox, cy + r - 1 + oy, r, color);
        tft.drawCircle(cx + r + 1 + ox, cy + r - 1 + oy, r, color);
        tft.drawLine(cx + ox, cy + r + oy, cx + ox, stemBot + oy, color);
        tft.drawLine(cx - 2 + ox, stemBot + oy, cx + 2 + ox, stemBot + oy, color);
    }
}
static void drawSpade(TFT_eSPI &tft, int cx, int cy, int sz, uint16_t color) {
    int r = sz * 3 / 10; if (r < 3) r = 3;
    int stemBot = cy + r + 1 + sz/3 + 1;
    for (int o = 0; o < 2; o++) {
        int ox = o, oy = o;
        tft.drawLine(cx + ox, cy - sz/2 + oy, cx - r + 1 + ox, cy - r + oy, color);
        tft.drawLine(cx + ox, cy - sz/2 + oy, cx + r - 1 + ox, cy - r + oy, color);
        tft.drawCircle(cx - r + ox, cy + 1 + oy, r, color);
        tft.drawCircle(cx + r + ox, cy + 1 + oy, r, color);
        tft.drawLine(cx + ox, cy + r + oy, cx + ox, stemBot + oy, color);
        tft.drawLine(cx - 2 + ox, stemBot + oy, cx + 2 + ox, stemBot + oy, color);
    }
}

static void drawSuitSymbol(TFT_eSPI &tft, int cx, int cy, int sz, uint8_t suit) {
    uint16_t col = g_themeColor;
    switch (suit) {
        case 0: drawClub(tft, cx, cy, sz, col);    break;
        case 1: drawDiamond(tft, cx, cy, sz, col);  break;
        case 2: drawHeart(tft, cx, cy, sz, col);    break;
        case 3: drawSpade(tft, cx, cy, sz, col);    break;
    }
}
static void drawStar(TFT_eSPI &tft, int cx, int cy, int sz, uint16_t color) {
    int rOuter = sz / 2, rInner = sz / 5;
    int px[5], py[5], ix[5], iy[5];
    for (int i = 0; i < 5; i++) {
        float ao = (i * 72.0f - 90.0f) * PI / 180.0f;
        float ai = ((i * 72.0f) + 36.0f - 90.0f) * PI / 180.0f;
        px[i] = cx + (int)(rOuter * cosf(ao)); py[i] = cy + (int)(rOuter * sinf(ao));
        ix[i] = cx + (int)(rInner * cosf(ai)); iy[i] = cy + (int)(rInner * sinf(ai));
    }
    for (int i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        tft.drawLine(px[i], py[i], ix[i], iy[i], color);
        tft.drawLine(ix[i], iy[i], px[j], py[j], color);
    }
}

// ── Card face (black fill, themed outline + symbols) ─────────────────────

static void drawCardFace(TFT_eSPI &tft, int x, int y, uint8_t card) {
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
    drawSuitSymbol(tft, x + CARD_W/2, y + CARD_H/2 + 3, 24, suit);
    tft.setTextDatum(BR_DATUM);
    tft.drawString(rankStr(rank), x + CARD_W - 5, y + CARD_H - 3);
}

// ── Card back ─────────────────────────────────────────────────────────────

static void drawCardBack(TFT_eSPI &tft, int x, int y) {
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

static void drawHoldFrame(TFT_eSPI &tft, int i, bool held, bool clear) {
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
