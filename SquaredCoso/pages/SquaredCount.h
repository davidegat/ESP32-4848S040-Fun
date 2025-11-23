#pragma once

/*
===============================================================================
   SQUARED — PAGINA "COUNTDOWN MULTIPLI" (HYPER-OPTIMIZED VERSION)

   - zero allocazioni String inutili
   - parsing ISO più veloce
   - sorting ottimizzato (insertion sort già corretto → solo snellito)
   - rendering più compatto
===============================================================================
*/

#include <Arduino.h>
#include <time.h>
#include "../handlers/globals.h"

// ============================================================================
// EXTERN
// ============================================================================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_HEADER;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern String sanitizeText(const String& in);

extern CDEvent cd[8];

String formatShortDate(time_t t);

// ============================================================================
// PARSE ISO (YYYY-MM-DD HH:MM) → time_t locale (OTTIMIZZATO)
// ============================================================================
static bool parseISOToTimeT(const String& iso, time_t& out)
{
  // Formato: "YYYY-MM-DD HH:MM"
  if (iso.length() < 16) return false;

  struct tm t = {};

  // parsing senza substring() temporanei
  t.tm_year = (iso.charAt(0)-'0')*1000 +
              (iso.charAt(1)-'0')*100  +
              (iso.charAt(2)-'0')*10   +
              (iso.charAt(3)-'0')      - 1900;

  t.tm_mon  = (iso.charAt(5)-'0')*10 + (iso.charAt(6)-'0') - 1;
  t.tm_mday = (iso.charAt(8)-'0')*10 + (iso.charAt(9)-'0');

  t.tm_hour = (iso.charAt(11)-'0')*10 + (iso.charAt(12)-'0');
  t.tm_min  = (iso.charAt(14)-'0')*10 + (iso.charAt(15)-'0');

  out = mktime(&t);
  return out > 0;
}


// ============================================================================
// FORMAT DELTA — tempo rimanente leggibile (HYPER-OPTIMIZED)
// ============================================================================
static String formatDelta(time_t target)
{
  time_t now;
  time(&now);

  long diff = long(target - now);

  if (diff <= 0)
    return (g_lang == "it" ? "scaduto" : "expired");

  long d = diff / 86400L; diff %= 86400L;
  long h = diff / 3600L;  diff %= 3600L;
  long m = diff / 60L;

  char buf[24];
  if (d > 0)
    snprintf(buf, sizeof(buf), "%ldg %02ldh %02ldm", d, h, m);
  else
    snprintf(buf, sizeof(buf), "%02ldh %02ldm", h, m);

  return String(buf);
}


// ============================================================================
// PAGINA COUNTDOWN — HYPER OPTIMIZED
// ============================================================================
void pageCountdowns()
{
  drawHeader("Countdown");
  int y = PAGE_Y;

  // struttura ottimizzata → niente String interne
  struct Row {
    const char* name;
    time_t      when;
  };

  Row list[8];
  int n = 0;

  // --------------------------------------------------------------------------
  // RACCOLTA EVENTI
  // --------------------------------------------------------------------------
  for (int i = 0; i < 8; i++) {
    if (cd[i].name.isEmpty() || cd[i].whenISO.isEmpty()) continue;

    time_t t;
    if (!parseISOToTimeT(cd[i].whenISO, t)) continue;

    list[n].name = cd[i].name.c_str();  // zero copie → solo puntatore
    list[n].when = t;
    n++;
  }

  // nessun countdown valido
  if (n == 0) {
    drawBoldMain(
      PAGE_X,
      y + CHAR_H,
      (g_lang == "it" ? "Nessun countdown" : "No countdowns"),
      TEXT_SCALE
    );
    return;
  }

  // --------------------------------------------------------------------------
  // ORDINAMENTO (insertion sort — già ideale per n ≤ 8)
  // --------------------------------------------------------------------------
  for (int i = 1; i < n; i++) {
    Row key = list[i];
    int j = i - 1;
    while (j >= 0 && list[j].when > key.when) {
      list[j + 1] = list[j];
      j--;
    }
    list[j + 1] = key;
  }

  gfx->setTextSize(TEXT_SCALE);

  // --------------------------------------------------------------------------
  // RENDER
  // --------------------------------------------------------------------------
  for (int i = 0; i < n; i++) {

    // sanitizzazione SOLO una volta
    String nm = sanitizeText(list[i].name);
    String dt = formatShortDate(list[i].when);
    String dl = formatDelta(list[i].when);

    // nome evento
    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->setCursor(PAGE_X, y + CHAR_H);
    gfx->print(nm);

    // data breve
    gfx->setTextColor(COL_HEADER, COL_BG);
    int y2 = y + CHAR_H * 2 + 4;
    gfx->setCursor(PAGE_X, y2);
    gfx->print("(");
    gfx->print(dt);
    gfx->print(")");

    // delta a destra
    gfx->setTextColor(COL_ACCENT2, COL_BG);
    gfx->setCursor(PAGE_X + 180, y2);
    gfx->print(dl);

    // avanzamento
    y += CHAR_H * 3 + 10;

    // separatore
    if (i < n - 1)
      gfx->drawFastHLine(PAGE_X, y, 440, COL_HEADER);

    if (y > 460) break;
  }
}

