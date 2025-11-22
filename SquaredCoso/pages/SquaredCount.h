#pragma once

/*
===============================================================================
   SQUARED — PAGINA “COUNTDOWN MULTIPLI”

   Questo modulo gestisce:
   - parsing ISO 8601 (YYYY-MM-DD HH:MM) → time_t locale
   - calcolo della differenza di tempo in formato leggibile (gg, hh, mm)
   - ordinamento dei countdown per data crescente
   - rendering della lista (nome, data breve, tempo rimanente)

   Comportamento:
   - mostra fino a 8 eventi configurati
   - ignora eventi senza nome o senza data
   - formatShortDate() è dichiarata qui ma definita in SquaredInfo.h
   - visualizzazione compatta, senza icone, coerente con le altre pagine testuali

===============================================================================
*/

#include <Arduino.h>
#include <time.h>
#include "../handlers/globals.h"


// ============================================================================
// EXTERN — risorse condivise fornite dal main
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

// countdown globale (fino a 8 eventi)
extern CDEvent cd[8];

// utility definita altrove
String formatShortDate(time_t t);


// ============================================================================
// PARSE ISO (YYYY-MM-DD HH:MM) → time_t locale
// ============================================================================
static bool parseISOToTimeT(const String& iso, time_t& out)
{
  if (iso.length() < 16)
    return false;

  struct tm t = {};
  t.tm_year = iso.substring(0, 4).toInt() - 1900;
  t.tm_mon  = iso.substring(5, 7).toInt() - 1;
  t.tm_mday = iso.substring(8, 10).toInt();
  t.tm_hour = iso.substring(11, 13).toInt();
  t.tm_min  = iso.substring(14, 16).toInt();

  out = mktime(&t);
  return out > 0;
}


// ============================================================================
// FORMAT DELTA — tempo rimanente leggibile (gg, hh, mm)
// ============================================================================
static String formatDelta(time_t target)
{
  time_t now;
  time(&now);

  long diff = long(target - now);

  if (diff <= 0)
    return (g_lang == "it" ? "scaduto" : "expired");

  long d = diff / 86400L;
  diff %= 86400L;

  long h = diff / 3600L;
  diff %= 3600L;

  long m = diff / 60L;

  char buf[32];
  if (d > 0)
    snprintf(buf, sizeof(buf), "%ldg %02ldh %02ldm", d, h, m);
  else
    snprintf(buf, sizeof(buf), "%02ldh %02ldm", h, m);

  return String(buf);
}


// ============================================================================
// RENDER PAGINA COUNTDOWN
// ============================================================================
void pageCountdowns()
{
  drawHeader("Countdown");
  int y = PAGE_Y;

  // struttura interna per ordinamento
  struct Row {
    const char* name;
    time_t when;
  };

  Row list[8];
  int n = 0;

  // --------------------------------------------------------------------------
  // RACCOLTA EVENTI VALIDI
  // --------------------------------------------------------------------------
  for (int i = 0; i < 8; i++) {
    if (cd[i].name.isEmpty() || cd[i].whenISO.isEmpty())
      continue;

    time_t t;
    if (!parseISOToTimeT(cd[i].whenISO, t))
      continue;

    list[n].name = cd[i].name.c_str();
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
  // ORDINAMENTO PER DATA
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
  // RENDERING
  // --------------------------------------------------------------------------
  for (int i = 0; i < n; i++) {

    String nm = sanitizeText(list[i].name);
    String dt = formatShortDate(list[i].when);
    String dl = formatDelta(list[i].when);

    // nome evento
    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->setCursor(PAGE_X, y + CHAR_H);
    gfx->print(nm);

    // data (in parentesi)
    gfx->setTextColor(COL_HEADER, COL_BG);
    int y2 = y + CHAR_H * 2 + 4;
    gfx->setCursor(PAGE_X, y2);
    gfx->print("(");
    gfx->print(dt);
    gfx->print(")");

    // tempo rimanente
    gfx->setTextColor(COL_ACCENT2, COL_BG);
    gfx->setCursor(PAGE_X + 180, y2);
    gfx->print(dl);

    // avanzamento verticale
    y += CHAR_H * 3 + 10;

    // separatore
    if (i < n - 1)
      gfx->drawFastHLine(PAGE_X, y, 440, COL_HEADER);

    if (y > 460) break;
  }
}

