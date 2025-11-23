#pragma once

#include <Arduino.h>
#include <time.h>

// ============================================================================
// EXTERN
// ============================================================================
extern void drawBoldMain(int16_t x, int16_t y, const String& raw);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);

extern const uint16_t COL_BG;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_ACCENT1;

extern Arduino_RGB_Display* gfx;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;

extern void drawHeader(const String& title);
extern void drawHLine(int y);

extern String g_lang;

// ============================================================================
// PAGINA OROLOGIO — VERSIONE HYPER-OPTIMIZED (stessi nomi, stessa logica)
// ============================================================================
inline void pageClock() {

  // ---- ORA LOCALE ----
  time_t now;
  struct tm t;
  time(&now);
  localtime_r(&now, &t);

  // ---- GREETING (no String) ----
  const int minutes = t.tm_hour * 60 + t.tm_min;
  const char* greet_it;
  const char* greet_en;

  if      (minutes < 360)  { greet_it = "Buonanotte!";  greet_en = "Good night!"; }
  else if (minutes < 690)  { greet_it = "Buongiorno!";  greet_en = "Good morning!"; }
  else if (minutes < 840)  { greet_it = "Buon appetito!"; greet_en = "Lunch time!"; }
  else if (minutes < 1080) { greet_it = "Buon pomeriggio!"; greet_en = "Good afternoon!"; }
  else if (minutes < 1320) { greet_it = "Buonasera!";    greet_en = "Good evening!"; }
  else                     { greet_it = "Buonanotte!";   greet_en = "Good night!"; }

  drawHeader(g_lang == "it" ? greet_it : greet_en);

  // ---- BUFFER TESTUALI ----
  char bufH[3];
  char bufM[3];
  char bufD[16];

  snprintf(bufH, sizeof(bufH), "%02d", t.tm_hour);
  snprintf(bufM, sizeof(bufM), "%02d", t.tm_min);
  snprintf(bufD, sizeof(bufD), "%02d/%02d/%04d",
           t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

  // ---- PARAMETRI ORARIO ----
  const int scale = 14;
  const int cw    = BASE_CHAR_W * scale;
  const int ch    = BASE_CHAR_H * scale;
  const int colonW = cw;         // per simmetria
  const int clockY = PAGE_Y + 40;

  // ---- CALCOLO CENTRATURA ----
  const int totalW = cw*2 + colonW + cw*2;
  const int clockX = (480 - totalW) >> 1;

  // ---- SFONDO ORARIO ----
  gfx->fillRect(0, clockY - 5, 480, ch + 150, COL_BG);

  // ---- RENDER ORA ----
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(scale);

  // ore
  gfx->setCursor(clockX, clockY);
  gfx->print(bufH);

  // ---- ":" DUE PUNTI, versione ottimizzata ----
  {
    const int colonX = clockX + cw*2;
    const int dotW = (colonW / 6 > 4) ? (colonW/6) : 4;
    const int dotH = (ch / 8   > 4) ? (ch/8)      : 4;

    const int centerX = colonX + ((colonW - dotW) >> 1);

    // precalcolo Y
    const int upY = clockY + (ch * 27) / 100;
    const int dnY = clockY + (ch * 62) / 100;

    gfx->fillRect(centerX, upY, dotW, dotH, COL_ACCENT1);
    gfx->fillRect(centerX, dnY, dotW, dotH, COL_ACCENT1);
  }

  // minuti
  gfx->setCursor(clockX + cw*2 + colonW, clockY);
  gfx->print(bufM);

  // ---- LINEA SEPARAZIONE ----
  const int sepY = clockY + ch + 18;
  drawHLine(sepY);

  // ---- DATA ----
  gfx->setTextSize(5);
  const int dateW = strlen(bufD) * BASE_CHAR_W * 5;

  const int dateY = sepY + 28;
  gfx->setCursor((480 - dateW) >> 1, dateY);
  gfx->print(bufD);

  // ---- GIORNO SETTIMANA ----
  static const char it_wd[7][12] PROGMEM = {
    "Domenica","Lunedi","Martedi","Mercoledi",
    "Giovedi","Venerdi","Sabato"
  };
  static const char en_wd[7][12] PROGMEM = {
    "Sunday","Monday","Tuesday","Wednesday",
    "Thursday","Friday","Saturday"
  };

  char wd[12];
  if (g_lang == "it") strcpy_P(wd, it_wd[t.tm_wday]);
  else                strcpy_P(wd, en_wd[t.tm_wday]);

  const int weekdayY = dateY + (BASE_CHAR_H * 5) + 20;

  gfx->setTextSize(4);
  const int wdW = strlen(wd) * BASE_CHAR_W * 4;
  gfx->setCursor((480 - wdW) >> 1, weekdayY);
  gfx->print(wd);

  // ripristino text size normale
  gfx->setTextSize(TEXT_SCALE);
}

