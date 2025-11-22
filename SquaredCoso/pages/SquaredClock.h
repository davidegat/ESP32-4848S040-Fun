#pragma once

#include <Arduino.h>
#include <time.h>

// ======================
// EXTERN DAL MAIN
// ======================
extern void drawBoldMain(int16_t x, int16_t y, const String& raw);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);

// colori definiti nel main (serve extern!)
extern const uint16_t COL_BG;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_ACCENT1;

// display
extern Arduino_RGB_Display* gfx;

// layout & helpers definiti nel main
extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;

extern void drawHeader(const String& title);
extern void drawHLine(int y);

extern String g_lang;

// ======================
// PROTOTIPO
// ======================
void pageClock();

// ======================
// IMPLEMENTAZIONE
// ======================
inline void pageClock() {

  // ---- Tempo locale ----
  time_t now;
  struct tm t;
  time(&now);
  localtime_r(&now, &t);

  // ---- Greeting ----
  int minutes = t.tm_hour * 60 + t.tm_min;

  const char* greet_it;
  const char* greet_en;

  if (minutes < 360) {
    greet_it = "Buonanotte!";
    greet_en = "Good night!";
  } else if (minutes < 690) {
    greet_it = "Buongiorno!";
    greet_en = "Good morning!";
  } else if (minutes < 840) {
    greet_it = "Buon appetito!";
    greet_en = "Lunch time!";
  } else if (minutes < 1080) {
    greet_it = "Buon pomeriggio!";
    greet_en = "Good afternoon!";
  } else if (minutes < 1320) {
    greet_it = "Buonasera!";
    greet_en = "Good evening!";
  } else {
    greet_it = "Buonanotte!";
    greet_en = "Good night!";
  }

  drawHeader(g_lang == "it" ? greet_it : greet_en);

  // ---- Buffer orario e data ----
  char bufH[3];
  char bufM[3];
  char bufD[16];

  snprintf(bufH, sizeof(bufH), "%02d", t.tm_hour);
  snprintf(bufM, sizeof(bufM), "%02d", t.tm_min);
  snprintf(bufD, sizeof(bufD), "%02d/%02d/%04d",
           t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

  // ---- Parametri layout ----
  const int scale = 14;
  const int charW = BASE_CHAR_W * scale;
  const int charH = BASE_CHAR_H * scale;
  const int colonW = charW;
  const int clockY = PAGE_Y + 40;

  const int totalW = (charW * 2) + colonW + (charW * 2);
  const int clockX = (480 - totalW) / 2;

  // ---- Sfondo orologio ----
  gfx->fillRect(0, clockY - 5, 480, charH + 150, COL_BG);

  // ---- Orario ----
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(scale);

  gfx->setCursor(clockX, clockY);
  gfx->print(bufH);

  // ---- Due punti ":" a rettangoli ----
  {
    int colonX = clockX + charW * 2;
    int dotW = max(4, colonW / 6);
    int dotH = max(4, charH / 8);

    int midX = colonX + ((colonW - dotW) >> 1);

    int upY = clockY + (charH * 27 / 100);
    int dnY = clockY + (charH * 62 / 100);

    gfx->fillRect(midX, upY, dotW, dotH, COL_ACCENT1);
    gfx->fillRect(midX, dnY, dotW, dotH, COL_ACCENT1);
  }

  gfx->setCursor(clockX + charW * 2 + colonW, clockY);
  gfx->print(bufM);

  // ---- Linea separazione ----
  int sepY = clockY + charH + 18;
  drawHLine(sepY);

  // ---- Data ----
  gfx->setTextSize(5);
  int dw = strlen(bufD) * BASE_CHAR_W * 5;

  int dateY = sepY + 28;
  gfx->setCursor((480 - dw) / 2, dateY);
  gfx->print(bufD);

  // ---- Giorno della settimana ----
  static const char* it_wd[7] = {
    "Domenica", "Lunedi", "Martedi", "Mercoledi",
    "Giovedi", "Venerdi", "Sabato"
  };

  static const char* en_wd[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
  };

  const char* weekday = (g_lang == "it" ? it_wd[t.tm_wday] : en_wd[t.tm_wday]);

  int weekdayY = dateY + (BASE_CHAR_H * 5) + 20;

  gfx->setTextSize(4);
  gfx->setCursor(
    (480 - (strlen(weekday) * BASE_CHAR_W * 4)) / 2,
    weekdayY
  );
  gfx->print(weekday);

  gfx->setTextSize(TEXT_SCALE);
}

