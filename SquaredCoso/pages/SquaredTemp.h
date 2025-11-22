#pragma once

#include <Arduino.h>
#include "../handlers/globals.h"

// ============================================================================
//  MODULO: TEMPERATURA 24H (interpolazione & grafico)
//
//  Funzioni offerte:
//      • fetchTemp24()  → scarica 7 valori giornalieri (mean temp) da Open-Meteo
//                          e genera 24 valori interpolati
//      • pageTemp24()   → disegna un grafico lineare 24h sul pannello
//
//  Caratteristiche:
//      - parsing manuale JSON (ricerca stringhe)
//      - 7 valori → espansi linearmente in 24 punti
//      - grafico lineare con min/max riportati in basso
//      - nessuna dipendenza esterna complessa
// ============================================================================


// ======================
// EXTERN DAL MAIN
// ======================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_HEADER;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int TEXT_SCALE;
extern const int CHAR_H;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);

extern int indexOfCI(const String&, const String&, int from);
extern bool httpGET(const String& url, String& out, uint32_t timeout);
extern bool geocodeIfNeeded();


// ============================================================================
// BUFFER TEMPERATURE 24h
// ============================================================================
static float t24[24] = { NAN };


// ============================================================================
// TROVA ']' CORRISPONDENTE ALLA '[' — parsing JSON base
// ============================================================================
static int findMatchingBracket(const String& src, int start) {
  if (start < 0 || start >= (int)src.length() || src[start] != '[')
    return -1;

  int depth = 0;
  const int N = src.length();

  for (int i = start; i < N; i++) {
    char c = src[i];
    if (c == '[') depth++;
    else if (c == ']') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}


// ============================================================================
// FETCH TEMPERATURA 24h (in realtà 7 valori → interpolati)
// ============================================================================
static bool fetchTemp24() {

  // reset buffer
  for (int i = 0; i < 24; i++)
    t24[i] = NAN;

  // serve lat/lon
  if (!geocodeIfNeeded()) return false;

  // endpoint open-meteo
  String url =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=" + g_lat +
    "&longitude=" + g_lon +
    "&daily=temperature_2m_mean"
    "&forecast_days=7&timezone=auto";

  String body;
  if (!httpGET(url, body, 12000))
    return false;

  // cerca sezione "daily"
  int posDaily = indexOfCI(body, "\"daily\"", 0);
  if (posDaily < 0) return false;

  // cerca array temperature_2m_mean
  int posMean = indexOfCI(body, "\"temperature_2m_mean\"", posDaily);
  if (posMean < 0) return false;

  int lb = body.indexOf('[', posMean);
  if (lb < 0) return false;

  int rb = findMatchingBracket(body, lb);
  if (rb < 0) return false;

  // estrai l'array grezzo
  String arr = body.substring(lb + 1, rb);

  // parsing dei 7 valori giornalieri
  float seven[7];
  for (int i = 0; i < 7; i++) seven[i] = NAN;

  int idx = 0;
  int start = 0;
  const int L = arr.length();

  while (idx < 7 && start < L) {

    // cerca numero
    int s = start;
    while (s < L && !((arr[s] >= '0' && arr[s] <= '9') || arr[s] == '-'))
      s++;
    if (s >= L) break;

    int e = s;
    while (e < L && ((arr[e] >= '0' && arr[e] <= '9') || arr[e] == '.' || arr[e] == '-'))
      e++;

    seven[idx] = arr.substring(s, e).toFloat();
    idx++;

    start = e + 1;
  }

  if (idx == 0) return false;

  // ancoraggi: 7 punti su 24 ore
  int anchors[7];
  for (int i = 0; i < 7; i++) {
    anchors[i] = (int)round(i * (24.0f / 6.0f));
    if (anchors[i] > 23) anchors[i] = 23;
  }

  // assegna valori ancora
  for (int i = 0; i < 7; i++)
    t24[anchors[i]] = seven[i];

  // interpolazione lineare tra ancore
  for (int a = 0; a < 6; a++) {
    int x1 = anchors[a];
    int x2 = anchors[a + 1];
    float y1 = seven[a];
    float y2 = seven[a + 1];

    int dx = x2 - x1;
    if (dx < 1) continue;

    float dy = (y2 - y1) / dx;

    for (int k = 1; k < dx; k++)
      t24[x1 + k] = y1 + dy * k;
  }

  return true;
}


// ============================================================================
// RENDER PAGINA – Grafico 24h con min/max
// ============================================================================
static void pageTemp24() {

  drawHeader(
    g_lang == "it" ? "Evoluzione temperatura"
                   : "Temperature trend");

  // calcolo valori min/max
  float mn = 9999, mx = -9999;
  for (int i = 0; i < 24; i++) {
    if (!isnan(t24[i])) {
      mn = min(mn, t24[i]);
      mx = max(mx, t24[i]);
    }
  }

  // nessun dato
  if (mn > mx || mn == 9999) {
    drawBoldMain(
      PAGE_X,
      PAGE_Y + CHAR_H,
      (g_lang == "it" ? "Dati non disponibili" : "Data not available"),
      TEXT_SCALE);
    return;
  }

  // area grafico
  int X = 20,  W = 440;
  int Y = PAGE_Y + 40, H = 260;

  gfx->drawRect(X, Y, W, H, COL_ACCENT2);

  // mapping verticale
  float range = mx - mn;
  if (range < 0.1f) range = 0.1f;

  // linee del grafico
  for (int i = 0; i < 23; i++) {
    if (isnan(t24[i]) || isnan(t24[i + 1])) continue;

    int x1 = X + (i     * W) / 23;
    int x2 = X + ((i+1) * W) / 23;

    int y1 = Y + H - (int)(((t24[i]     - mn) / range) * H);
    int y2 = Y + H - (int)(((t24[i + 1] - mn) / range) * H);

    gfx->drawLine(x1, y1, x2, y2, COL_ACCENT1);
  }

  // legenda min/max sotto al grafico
  drawBoldMain(
    PAGE_X,
    Y + H + 20,
    (g_lang == "it"
       ? String((int)mn) + " min - " + String((int)mx) + " max"
       : String((int)mn) + " low - " + String((int)mx) + " high"),
    TEXT_SCALE + 1);
}

