#include <TFT_eSPI.h>
#include <SPI.h>
TFT_eSPI tft = TFT_eSPI();
#define CYD
#undef  BUTTONS
#define ROTATION 3

#ifdef TOUCH

#include <XPT2046_Touchscreen.h>
#define TS_SCLK 25
#define TS_MISO 39
#define TS_MOSI 32
#define TS_CS   33
#define TS_IRQ  36  // Opcionalan
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(TS_CS, TS_IRQ);

static int tx,ty,touch;

void readTouch() {
  touch = 0;
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    // Mapiranje touch-a na koordinatni sistem ekrana (320x240)
    //tx = map(p.x, 250, 3750, 0, 320); 
    //ty = map(p.y, 300, 3700, 0, 240);
    tx = (p.x-250)*32/350;
    ty = (p.y-300)*24/340;
    touch = 1;
  }
}
#endif

#include "cards.h"
//#include "jolly_logo.h"
//#include "game_over.h"
//#include "c64cf.h"
//#include "SansSerif_bold_10.h"
//#include "SansSerif_bold_16.h"
//#include "SansSerif_bold_19.h"
//#include "SansSerif_bold_36.h"
#define MAX_JOKERS 1

extern void drawPayTable();

void setup() {

#ifdef BUTTONS
  pinMode(A0, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);
  pinMode(A2, INPUT_PULLUP);
  pinMode(A3, INPUT_PULLUP);
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
#endif

  Serial.begin(115200);
  tft.init();
  tft.setRotation(ROTATION);

  tft.fillScreen(0x0008); // Originalna tamno-plava
  tft.invertDisplay(0);
  tft.writecommand(TFT_MADCTL);
  tft.writedata(TFT_MAD_MY | TFT_MAD_MV | TFT_MAD_BGR);

#ifdef TOUCH
  touchSPI.begin(TS_SCLK, TS_MISO, TS_MOSI, TS_CS);
  ts.begin(touchSPI);
  ts.setRotation(ROTATION);
#endif

  drawPayTable();
  randomSeed(analogRead(34) + micros()); // Kvalitetniji seed
}