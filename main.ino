/**
 * ── DEPRECATED ──────────────────────────────────────────────────────────────
 * This is the LEGACY v1 single-file version.  The active code lives under
 * src/ (main.cpp + cards.h + holdem.cpp + config.h + theme.cpp + lgfx_cyd.h).
 * Build with PlatformIO (platformio.ini) — NOT this .ino file directly.
 * ────────────────────────────────────────────────────────────────────────────
 *
 * ESP32 POKER - UNO POKER port to CYD - work in progress
 * * Features: 
 * - LZSS Bitmap Decompression (Direct to TFT)
 * - Hybrid Input: Physical Buttons (Port C, D5, D6) + Resistive Touch
 * - Autoplay for demonstration - when is no input defined
 */

//#define BUTTONS // Uncomment to enable physical button support
//#define TOUCH   // Uncomment to enable touch screen support

#include "config.h" //for CYD use "config_for_CYD.h" instead of "config.h"
                    //choose "ESP32-WROOM-DA Module" and upload

// --- Game Logic Constants ---
const char* const wins[] = {
  "FIVE OF A KIND", "Royal Flush", "Street Flush", "Poker",
  "Full House", "Flush", "Street", "Three of a Kind",
  "Two pairs", "High pair"
};
const uint16_t payouts[] = {1000, 500, 100, 40, 10, 7, 5, 3, 2, 1};

// --- Global Game State ---
unsigned long credits = 100;    // Credits must be long for high scores
uint8_t dice = 0;               // Cycle 0-255 for pseudo-random visual offsets
#define bet 5
uint8_t gamePhase = 0;          // 0: Idle, 1: Hold, 2: Win Choice, 3: Gamble
int8_t win = -1;                // Winning hand index (-1 = none)
unsigned long payout = 0;       // Current potential win

uint8_t hand[5];                // Current cards in hand
bool hold[5] = {false};         // Hold status per card
bool used[60] = {false};        // Deck tracking (to avoid duplicates)
uint8_t jokers = 0;


// Layout constants (Geometry)
#define wbw  200 // Winbox width
#define wbh  90  // Winbox height
#define ptlx 6   // Paytable left X
#define ptrx 204 // Paytable right X
#define ptty 6   // Paytable top Y
#define scx  264 // Status center X

void loop() {

#ifdef TOUCH
  readTouch(); // Check manual touch input
  #ifndef BUTTONS
  if (!touch) return; // Exit loop if no input to save CPU
  #endif
#endif

  uint8_t buttons = 0;

#ifdef BUTTONS
  // Bitwise reading of Port C for speed and space efficiency
  buttons = ( (~PINC) & 0x0F )
          | (digitalRead(4) == LOW ? (1 << 4) : 0)
          | (digitalRead(5) == LOW ? (1 << 5) : 0)
          | (digitalRead(6) == LOW ? (1 << 6) : 0);
  /*if (buttons!=0){
      updateTextSize(1);
      updateTextColor(0xFFFF, 0x0008);
      drawCenterNumber(buttons, scx, ptty + 1 + 9 * 6);
    }*/
  #ifndef TOUCH
  if (buttons == 0) return;
  #else
  if ((buttons == 0) && !touch) return;
  #endif
#endif

  // --- Phase 1: Card Selection (HOLD) ---
#if defined BUTTONS || defined TOUCH
  if (gamePhase == 1) {
    for (uint8_t i = 0; i < 5; i++) {
      bool pressed = ((buttons >> i) & 0x01 != 0);
      #ifdef TOUCH
      int cardX = 6 + i * 62;
      pressed |= (tx > cardX && tx < cardX + 60 && ty > 140);
      #endif
      if (pressed) {
        hold[i] = !hold[i];
        drawHoldFrame(i, false);
      }
    }
  }
#endif

  uint8_t btnIdx = 8;

#if defined BUTTONS || defined TOUCH
  // --- Phase 2: Gamble / Double Logic ---
  if (gamePhase == 2){
  #ifdef TOUCH
    if (((touch)&&(tx>ptrx+12)&&(ty>50)&&(ty<90))||((buttons>>6 & 1) != 0)) takeMoneyAndRun();
  #else
    if ((buttons>>6 & 1) != 0) takeMoneyAndRun();
  #endif
  }
#endif

  // --- Phase 3: Gamble / Double Logic ---
  if (gamePhase == 3) {
#if defined BUTTONS || defined TOUCH
  #ifdef TOUCH
    if ((touch)&&(tx>ptrx+12)) btnIdx = (ty - 48) / 64; // 0: Collect/Collect Half, 1: High, 2: Low
//    else if (((touch)&&(tx<ptrx))&&(touch && (ty > 90 && ty < 140))) btnIdx=3;
  #endif
    if ((buttons>>6 & 1) != 0) btnIdx = 3;  // Colect All
    else if ((buttons>>1 & 1) != 0) btnIdx = 1; // Low
    else if ((buttons>>2 & 1) != 0) btnIdx = 0; // Collect Half
    else if ((buttons>>3 & 1) != 0) btnIdx = 2; // High
#else
    btnIdx = random(3);
#endif
    if (btnIdx==3) takeMoneyAndRun();
    else if (btnIdx == 0) collectWin(payout / 2); // Collect half
    else if ((payout < 500000000) && (btnIdx < 3)) {
      uint8_t h = (btnIdx == 2) ? 1 : 0;   // Choice: 1=Low, 2=High
#if !defined BUTTONS && !defined TOUCH
      // Slightly better odds in autoplay depending on credits
      // 0=Loss, 1..3=Win, ensure it doesn't lose forever
      uint8_t r = (random(2 + (credits < 100) + (credits + payout < 200)) == 0);
#else
      // 0=Win, 1=Loss
      uint8_t r = random(2);
#endif

      payout = (1 - r) * payout * 2;     // Double or nothing

      // XOR Formula: random(24) gives card, offset 28*(r^h) selects range
      uint8_t nextCardIdx = 4 + random(24) + 28 * (r ^ h);

      // Draw new card with "dice" offset for visual deck effect
      drawLZSSCard(6 + 11 * (dice % 14) + 2 * (dice / 14), 134 + 2 * (dice / 14), nextCardIdx);
      delay(400);
      dice++; // Move position for next flip

      if (r == 1) { // If lost
        takeMoneyAndRun();
      } else {
        updatePayout();
        drawLZSSCard(6 + 11 * (dice % 14) + 2 * (dice / 14), 134 + 2 * (dice / 14), 63); // Cover with back
      }
    }
  }

  // Central command (Enter - Deal/Draw/Collect)
#if defined BUTTONS || defined TOUCH
  #ifdef TOUCH
  if ((((buttons>>5)&1)!=0) || ((touch && (ty > 90 && ty < 140))&&((tx<ptrx)||(gamePhase != 3)))) {
  #else
  else if ((((buttons>>5)&1)!=0) != 0) {
  #endif
#else
  else if (true) {
#endif
    if (gamePhase == 0) startDeal();
    else if (gamePhase == 1) finalDraw();
    else if (gamePhase == 2) startBet();
    else if (gamePhase == 3) takeMoneyAndRun();
    else if (gamePhase == 4) takeMoneyAndRun();
  }

  delay(300);
}

// ────────────────────────────────────────────────
// Helper functions
// ────────────────────────────────────────────────


// ────────────────────────────────────────────────
// Drawing & UI functions
// ────────────────────────────────────────────────

void clearMessage() {
  tft.fillRect(0, 100, 320, 32, 0x0008);
}
void drawMessage(const char* msg, uint16_t col, bool toLeft = 0) {
  updateTextSize(3);
  updateTextColor(col, 0x0008);
  clearMessage();
  drawCenterText(msg, toLeft ? ptlx + wbw / 2 : 160, 108);
}

void drawPayTable() {
  tft.fillRect(ptlx, ptty - 1, wbw+1, wbh, 0x0008);
  updateTextSize(1);
  updateTextColor(0x8FF1, 0x0008);
  for (int8_t i = 0; i < 10; i++) {
    drawText((const char*)wins[i], ptlx, ptty + i * 9);
    drawRightNumber(payouts[i] * bet, ptrx, ptty + i * 9);
  }
  updateCredits();
  drawMessage("PRESS TO DEAL", 0x7fff);
}

void updateCredits() {
  updateTextSize(1);
  updateTextColor(0xFFFF, 0x0008);
  drawCenterText("CREDITS", scx, ptty);
  updateTextSize(3);
  updateTextColor(0xFFEF, 0x0008);
  drawCenterNumber(credits, scx, ptty + 1 + 9 + 4);
}

void updatePayout() {
  updateTextSize(4);
  updateTextColor(0xFFFF, 0xE000);
  drawCenterNumber(payout, ptlx + 2 + wbw / 2, ptty + 42);
}

#ifdef TOUCH

void clearButton(int i=0) {
  tft.fillRect(ptrx + 14, 50 + i * 64, 300 - ptrx, 43, 0x0008);
}

void drawButton(int i, char* s, uint16_t col, uint16_t bg) {
  updateTextColor(col,bg);
  updateTextSize(2);
  tft.drawRect(ptrx + 14, 50 + i * 64, 300 - ptrx, 42, 0xFFFF);
  tft.fillRect(ptrx + 15, 51 + i * 64, 298 - ptrx, 40, bg); //0x2d35
  drawCenterText(s, scx+1, 2 + 9 * 7+i*7*9);
}
#endif

void clearWinBox() {
  tft.fillRect(ptlx, ptty - 1, wbw, wbh, 0xE000);
  tft.drawRect(ptlx, ptty - 1, wbw+1, wbh, 0xFFFF);
  updateTextSize(3);
  updateTextColor(0xFFFF, 0xE000);
}

/*void pressToDeal() {
  drawMessage("PRESS TO DEAL", 0x7fff);
}*/

void highlightWin(int col){
  updateTextSize(1);
  updateTextColor(col, 0x0008); // Pale yellow
  drawText((const char*)wins[win], ptlx, ptty + win * 9);
  drawRightNumber(payout, ptrx, ptty + win * 9);
}

void clearHand() {
  int x;
  for (uint8_t i = 0; i < 5; i++) {
    x = 6 + i * 62;
    tft.fillRect(x - 2, 132, 64, 104, 0x0008);
    delay(100);
  }
}

void drawHand() {
  int x;

  // First show card backs for non-held cards
  for (uint8_t i = 0; i < 5; i++) {
    if (!hold[i]) {
      x = 6 + i * 62;
      drawLZSSCard(x, 134, 63);
    }
    drawHoldFrame(i, true);
    delay(100);
  }

  // Then reveal actual cards
  for (uint8_t i = 0; i < 5; i++) {
    if (!hold[i]) {
      x = 6 + i * 62;
      drawLZSSCard(x, 134, hand[i]);
      delay(50);
    }
  }
}

void drawHoldFrame(int i, bool clear) {
  int x = 6 + i * 62;
  uint16_t col = clear      ? 0x0008 :
                 hold[i]    ? 0x2D35 :
                              0x0008;

  int lbc = !hold[i] && (i > 0) && hold[i - 1];     // left border correction
  int rbc = !hold[i] && (i < 4) && hold[i + 1];     // right border correction

  tft.drawRect(x - 2 + lbc * 2, 132, 64 - lbc * 2 - rbc * 2, 92, col);
  tft.drawRect(x - 1 + lbc,     133, 62 - lbc - rbc * 2,     90, col);

  updateTextSize(1);
  updateTextColor(hold[i] ? 0xA124 : 0x2D35, 0x0008);

  tft.fillRect(x + 12, 95 + 132, 4 * 8 + 4, 11, 0x0008); // clear old text

  if (hold[i])
    drawCenterText("HOLD", x + 29, 95 + 134);
  else if (!clear)
    drawCenterNumber(i + 1, x + 29, 95 + 134);
}

// ────────────────────────────────────────────────
// Game flow functions
// ────────────────────────────────────────────────

void dealHand() {
  clearMessage();
  for (int i = 0; i < 5; i++) {
    int j;
    if (!hold[i]) {
      do j = random(52+MAX_JOKERS*(jokers==0)); // joker is at the end; 52 cards + MAX_JOKERS
      while (used[j]);
      if (j>51) { jokers++; j=52+random(4); }
      hand[i] = j%4+((j/4+1)%14)*4; // rotate deck so jokers at beginning
      used[j] = true;
    }
  }
  drawHand();
}

void startDeal() {
#if defined BUTTONS || defined TOUCH
  #ifdef TOUCH
  clearButton();
  #endif
  clearMessage();
#endif
  clearHand();
#if !defined BUTTONS && !defined TOUCH
  clearMessage();
#endif
  if (credits < bet) {
    drawMessage("NO CREDITS", 0xF88F);
    delay(1000);
    payout=100;
    clearWinBox();
    drawCenterText("NEW GAME", ptlx + wbw / 2, 18);
    updatePayout();
    drawMessage("PRESS FOR NEW GAME", 0xFFEF);
    gamePhase = 4;
    return;
  }
  credits -= bet;
  updateCredits();
  memset(hold, 0, sizeof(hold));
  memset(used, 0, sizeof(used));
  jokers=0;
  dealHand();
  evaluateWin(true);
  if (win >= 0)
    highlightWin(0xFFEF); // Pale yellow
  drawMessage("(1-5) HOLD, THEN DRAW", 0x2D35);
  gamePhase = 1;
}

void finalDraw() {
  if (win>=0) 
    highlightWin(0x8FF1);
  dealHand();
  evaluateWin(false);
  if (win >= 0) showBigWin();
  else {
    gamePhase = 0;
    drawMessage("PRESS TO DEAL", 0x7fff);
  }
}

// ────────────────────────────────────────────────
// Game flow functions (continued)
// ────────────────────────────────────────────────

void showBigWin() {
  clearWinBox();
  drawCenterText((const char*)wins[win], ptlx + wbw / 2, 18);
  updatePayout();
#ifdef TOUCH
  drawButton(0,"COLLECT",0xFEA0,0x00EF); //0x00EF, 0xFEA0
#endif
  drawMessage("PRESS TO BET FOR DOUBLE", 0xFFEF); //
  gamePhase = 2;
}

void startBet() {
  clearMessage();
  clearHand();
  drawMessage("COLLECT", 0xFFEF, true);

#ifdef TOUCH
  // Text offset adjustment when touch buttons are visible
  #define TEXT_X_OFFSET  70
#else
  #define TEXT_X_OFFSET  140 //200 
#endif

#ifdef TOUCH
  // Draw touch gamble buttons
  drawButton(0,"HALF",0xFFEF,0x09EF); //0x00EF
  drawButton(1,"LOW",0xFFEF,0x78E9);
  drawButton(2,"HIGH",0xFFEF,0x78E9);
#endif

  // Instructions for buttons (shown always)
  updateTextSize(1);
  updateTextColor(0xA7EB);
  drawText("<2> FOR LOW A-6 ", TEXT_X_OFFSET, 8 + 9 * 15);
  drawText("<4> FOR HIGH 8-K", TEXT_X_OFFSET, 8 + 9 * 19);
  updateTextColor(0xE0FF);
  drawText("<3> TO CUT HALF ", TEXT_X_OFFSET, 8 + 9 * 17);
  updateTextColor(0x7fff);
  drawText("<C> TO COLLECT  ", TEXT_X_OFFSET, 8 + 9 * 22);

  dice = 0;
  drawLZSSCard(ptlx, 134, 63);
  updateTextColor(0xFFFF, 0xE000);
  gamePhase = 3;
}

void collectWin(long amount) {
  if (amount <= 0) return;

  while (amount > 0) {
    uint16_t step = amount <= 100    ? 1      :
                    amount <= 1000   ? 100    :
                    amount < 10000   ? 1000   :
                    amount < 100000  ? 10000  :
                    amount < 1000000 ? 100000 : 1000000;

    amount -= step;
    payout -= step;
    credits += step;

    // Update only essential parts to avoid flickering
    updatePayout();
    updateCredits();
    delay(10);
  }
}

void takeMoneyAndRun() {
  if (payout > 0)
    collectWin(payout);

  delay(1000);

#ifdef TOUCH
  for (int8_t i = 0; i < (gamePhase==2? 1 : 3); i++) clearButton(i);
#endif

  gamePhase = 0;
  drawPayTable();
}

// ────────────────────────────────────────────────
// Win evaluation
// ────────────────────────────────────────────────

void evaluateWin(bool doHold) {

  uint8_t numbs[14] = {0};  // 0 = Jokers, 1 = Ace, 2 = 2, ..., 13 = King
  uint8_t suits[4] = {0};   // Count of each suit (0=clubs, 1=diamonds, 2=hearts, 3=spades)

  uint8_t minimum = 13,     // Lowest rank in hand (excluding jokers)
          maximum = 2;      // Highest rank in hand

  uint8_t count = 1;        // Highest number of cards of the same rank
  uint8_t rank = 0;         // Rank that has the highest count
  
  uint8_t pair1 = 0,        // For detecting two pairs
          pair2 = 0;  
          
  // Count ranks and suits
  for (uint8_t i = 0; i < 5; i++) {
    numbs[hand[i] / 4]++;                       // Rank count (0=joker, 1=A, 2=2, ..., 13=K)

    if (hand[i] / 4 > 0) suits[hand[i] % 4]++;  // Suit count (ignore jokers)

    // Track min/max rank (ignoring jokers for straight detection)
    if (hand[i] / 4 > 1 && hand[i] / 4 < minimum) 
      minimum = hand[i] / 4;

    if (hand[i] / 4 > maximum) 
      maximum = hand[i] / 4;
  }

  // Find the most frequent rank and detect pairs
  for (uint8_t i = 1; i <= 13; i++) {
    if (numbs[i] > count) {
      count = numbs[i];
      rank = i;
    }
    // Simple pair detection (not perfect, but works for two pairs)
    if (numbs[i % 13 + 1] == 2) {
      pair2 = pair1;
      pair1 = i % 13 + 1;
    }
  }

  // Flush check (same suit, jokers count as wild)
  bool flush = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (suits[i] + numbs[0] >= 5) flush = true;
  }

  // Straight check (with ace-low and ace-high handling)
  bool street = false;
  if ((((maximum - minimum < 5) && (numbs[1] == 0)) ||           // normal straight, no ace
       ((maximum <= 5) && (numbs[1] == 1)) ||                    // wheel (A-2-3-4-5)
       ((minimum >= 10) && (numbs[1] == 1))) && (count == 1)) {  // broadway (10-J-Q-K-A)
    street = true;
  }

  // Default: no win
  win = -1;
  
  // 0 – Five of a kind
  if (count + numbs[0] >= 5)                    win = 0;
  // 1 – Royal Flush
  else if (street && flush && maximum >= 10 &&
           ((maximum - minimum < 5 && numbs[1]==0) || (minimum >=10 && numbs[1]==1)))
                                                win = 1;
  // 2 – Straight Flush
  else if (street && flush)                     win = 2;
  // 3 – Four of a kind (poker)
  else if (count + numbs[0] == 4)               win = 3;
  // 4 – Full House
  else if ((count == 3 && pair1) || (pair1 && pair2 && numbs[0]))
                                                win = 4;
  // 5 – Flush
  else if (flush)                               win = 5;
  // 6 – Straight
  else if (street)                              win = 6;
  // 7 – Three of a kind
  else if (count + numbs[0] == 3)               win = 7;
  // 8 – Two pairs
  else if (pair1 && pair2)                      win = 8;
  // 9 – High pair (Jacks or better) + joker rules
  else if ((pair1 > 10 || pair1 == 1) ||              // J,Q,K,A pair
           (numbs[0] && (maximum > 10 || numbs[1])))  // joker + high card
                                                win = 9;

  // Auto-hold logic (only on first deal)
  if (doHold) {
    for (uint8_t i = 0; i < 5; i++) {
      // Hold cards that contribute to the win
      if (
        (((hand[i] / 4) == pair1) || ((hand[i] / 4) == pair2) || ((hand[i] / 4) == rank) || ((hand[i] / 4) == 0)) ||
        ((maximum > 10) && (pair1 == 0) && ((hand[i] / 4) == maximum) && (win == 9)) ||
        flush || street
      ) {
        hold[i] = true;
      }
      // Special case: ace in low-pair win
      else if ((maximum <= 10) && (pair1 == 0) && ((hand[i] / 4) == 1) && (win == 9)) {
        hold[i] = true;
      }

      drawHoldFrame(i, false);
    }
  }

  // Calculate potential payout
  payout = (win >= 0 ? payouts[win] * bet : 0);
}


// ────────────────────────────────────────────────
// LZSS Decompression Engine
// Core graphics system – decodes compressed card data from PROGMEM
// ────────────────────────────────────────────────
// 0-visina 41, 1-visina 51, 2-visina 64, 3-visina 82

uint8_t lzssBuf[160];
uint16_t lineBuf[320];
uint16_t lineBufIdx,lzssBufIdx;
uint8_t window[LZSS_WINDOW];
int cardPixels, cardLine, cardLines, cardBpp, cardPaleteIdx;
int cardBytesPerLine;
int cardX, cardY;

// Helper struct for bit-level reading from PROGMEM
struct BitReader {
  const uint8_t* ptr;
  uint8_t buffer;
  uint8_t count;
  uint32_t pos;

  uint8_t readBit() {
    if (count == 0) {
      buffer = pgm_read_byte(ptr + pos++);
      count = 8;
    }
    return (buffer >> --count) & 1;
  }

  uint16_t readBits(uint8_t n) {
    uint16_t val = 0;
    for (uint8_t i = 0; i < n; i++) val = (val << 1) | readBit();
    return val;
  }
};

void drawLZSSCard(int x, int y, int cardIdx) {
  BitReader br;
  if (cardIdx==64){
    // Logo rendering
    br = { jolly_logo, 0, 0, 0 };
    cardPaleteIdx=16;
    cardBpp=4;
    cardPixels=LZSS_LOGO_WIDTH;
    cardLines=LZSS_LOGO_HEIGHT;
    cardBytesPerLine=cardPixels/2;
  }else{
    // Card border rendering
    tft.drawRect(x,y,60,88,0);
    tft.drawLine(x+58,y+1,x+58,y+86,0x7BCF);
    if (cardIdx==63){
      tft.drawRect(x+1,y+1,57,86,0xFFFF); //c0e0
      tft.drawRect(x+2,y+2,55,84,0xFFFF); //c0e0
  //    tft.fillRect(x+3,y+3,53,82,0x78E9); //c0e0
    }else //drawCardBorder(x,y);
      tft.fillRect(x+1,y+1,57,86,0xFFFF);
    // Card rendering
    uint32_t offset = pgm_read_dword(&(card_offsets[cardIdx]));
    br = { card_data, 0, 0, offset };
    cardPaleteIdx=cardIdx/4;
    cardBpp=CARDS_BPP;
    cardPixels=LZSS_CARD_WIDTH;
    cardBytesPerLine=cardPixels/2;
    cardLines=height_map[card_type[cardIdx]];
  }
  
  uint8_t wPtr = 0;

  lineBufIdx=0;
  cardLine=0; cardX=x; cardY=y;
  int totalBytes = (cardPixels * cardLines * cardBpp + 7) / 8;
  int decoded = 0;

  // LZSS decompression loop
  while (decoded < totalBytes) {
    if (br.readBit()) { // Match
      uint8_t dist = br.readBits(8);
      uint8_t len = br.readBits(4);
      for (uint16_t i = 0; i < len; i++) {
        uint8_t b = window[(uint8_t)(wPtr - dist)];
        processByte(b); // Tvoja funkcija za crtanje bajta
        //if (wPtr<LZSS_WINDOW) 
        window[wPtr++] = b;
        decoded++;
      }
    } else { // Literal
      uint8_t b = br.readBits(8);
      processByte(b);
      //if (wPtr<LZSS_WINDOW) 
      window[wPtr++] = b;
      decoded++;
    }
  }
}
uint16_t swap16(uint16_t val) {
  return (val << 8) | (val >> 8);
}
void processByte(uint8_t b){
  lzssBuf[lzssBufIdx++]=b;
  if (!(lzssBufIdx<cardBytesPerLine)) {
    if (cardBpp==1)
      for (int x = 0; x < cardPixels; x++) {
        uint8_t byte = lzssBuf[x >> 3];
        uint8_t bit = (byte >> (7 - (x & 7))) & 0x01;
        lineBuf[x] = pgm_read_word(&palette16[cardPaleteIdx][bit]);
      }
    else if (cardBpp==2)
      for (int x = 0; x < cardPixels; x++) {
        uint8_t byte = lzssBuf[x >> 2];
        uint8_t shift = (6 - 2 * (x & 3));
        uint8_t idx = (byte >> shift) & 0x03;
        lineBuf[x] = pgm_read_word(&palette16[cardPaleteIdx][idx]);
      }
    else if (cardBpp==4)
      for (int x = 0; x < cardPixels; x++) {
        uint8_t byte = lzssBuf[x >> 1];
        uint8_t shift = (4 - 4 * (x & 1));
        uint8_t idx = (byte >> shift) & 15;
        lineBuf[x] = pgm_read_word(&palette16[cardPaleteIdx][idx]);
      }
    writeBytes();
    lzssBufIdx=0; cardLine++;
  }
}

void writeBytes() {
  tft.pushImage (cardX+2, cardY+3+cardLine, cardPixels-1, 1, lineBuf);
  if ((82-cardLine)>cardLines){
    for (int i=0; i<cardBytesPerLine; i++){uint16_t temp=lineBuf[cardPixels-2-i]; lineBuf[cardPixels-2-i]=lineBuf[i]; lineBuf[i]=temp;}
    tft.pushImage (cardX+2, cardY+84-cardLine, cardPixels-1, 1, lineBuf);
  }
}

void displayLogo(){
  drawLZSSCard(8,128,64);
}