#pragma once

/*
===============================================================================
   SQUARED — PAGINA “CRYPTO – BITCOIN” (HYPER-OPTIMIZED)

   - Fetch da CoinGecko (prezzo BTC + variazione 24h)
   - Parsing JSON minimale con scansione diretta del buffer
   - Formattazione numerica fiat con separatori migliaia
   - Calcolo valore totale BTC posseduti
   - Rendering: prezzo grande, % 24h, valore totale

   NOTE:
   - Nessun grafico / sparkline / frecce direzionali.
   - Valuta definita da g_fiat (configurabile via WebUI).
   - g_btc_owned opzionale.
===============================================================================
*/

#include <Arduino.h>
#include "../handlers/globals.h"

// ============================================================================
// EXTERN — risorse condivise
// ============================================================================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern String g_lang;
extern String g_fiat;
extern double g_btc_owned;

extern bool httpGET(const String& url, String& out, uint32_t timeout);
extern int  indexOfCI(const String& src, const String& key, int from);
extern void drawHeader(const String& title);
extern void drawHLine(int y);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);

// ============================================================================
// STATO MODULO CRYPTO
// ============================================================================
static double   cr_price          = NAN;   // prezzo BTC in fiat
static double   cr_prev_price     = NAN;   // prezzo precedente
static double   cr_chg24          = NAN;   // % variazione 24h
static uint32_t cr_last_update_ms = 0;

// ============================================================================
// PARSER JSON — cerca "key": NUMBER (leggero, senza substring())
// ============================================================================
static bool crFindNumberKV(const String& body,
                           const char* key,
                           int from,
                           double& outVal)
{
  char kbuf[48];
  snprintf(kbuf, sizeof(kbuf), "\"%s\"", key);

  const char* base = body.c_str();
  const char* start = base + from;

  const char* p = strstr(start, kbuf);
  if (!p) return false;

  // cerca ':'
  const char* colon = strchr(p, ':');
  if (!colon) return false;

  const char* s = colon + 1;

  // skip spazi e controlli vari
  while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;

  // strtod per numero (gestisce segno e punto decimale)
  char* endptr = nullptr;
  double val = strtod(s, &endptr);
  if (endptr == s) return false;   // nessun numero valido

  outVal = val;
  return true;
}

// ============================================================================
// FORMATTER — cifra fiat con separatori migliaia (senza .00 se non serve)
// ============================================================================
static String crFmtFiat(double v)
{
  if (isnan(v)) return "--.--";

  long long ip = (long long)v;
  double f = v - (double)ip;
  if (f < 0) f = 0;

  int cents = (int)(f * 100.0 + 0.5);
  if (cents >= 100) {
    ip++;
    cents -= 100;
  }

  // parte intera con separatori
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", ip);
  String sInt = buf;

  String out;
  out.reserve(sInt.length() + 4);
  int cnt = 0;

  for (int i = sInt.length() - 1; i >= 0; --i) {
    out = sInt.charAt(i) + out;
    if (++cnt == 3 && i > 0) {
      out = "'" + out;
      cnt = 0;
    }
  }

  // centesimi zero → niente parte decimale
  if (cents == 0) return out;

  char cb[8];
  snprintf(cb, sizeof(cb), "%02d", cents);
  return out + "." + cb;
}

// ============================================================================
// FORMATTER — valore BTC con 8 decimali (notazione europea)
// ============================================================================
static String crFmtBTC(double v)
{
  if (isnan(v)) return "--";
  String s = String(v, 8);
  s.replace('.', ',');
  return s;
}

// ============================================================================
// FETCH DATA — CoinGecko API
// ============================================================================
static bool fetchCrypto()
{
  if (!g_fiat.length())
    g_fiat = "CHF";

  String fiat = g_fiat;
  fiat.toLowerCase();  // chiave JSON in minuscolo

  String url =
    "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin"
    "&vs_currencies=" + fiat +
    "&include_24hr_change=true";

  String body;
  if (!httpGET(url, body, 10000))
    return false;

  int b = indexOfCI(body, "\"bitcoin\"", 0);
  if (b < 0) return false;

  // prezzo
  double price = NAN;
  if (!crFindNumberKV(body, fiat.c_str(), b, price))
    return false;

  // variazione 24h
  double pct = NAN;
  {
    String k = fiat + "_24h_change";
    (void)crFindNumberKV(body, k.c_str(), b, pct);  // se fallisce → pct = NAN
  }

  // aggiorna stato
  cr_prev_price      = cr_price;
  cr_price           = price;
  cr_chg24           = pct;
  cr_last_update_ms  = millis();

  return true;
}

// ============================================================================
// RENDER — prezzo, % 24h, valore totale
// ============================================================================
static void pageCrypto()
{
  const bool it = (g_lang == "it");

  drawHeader("Bitcoin");
  int y = PAGE_Y;

  // --------------------------------------------------------------------------
  // BLOCCO 1 — prezzo grande centrato
  // --------------------------------------------------------------------------
  gfx->setTextSize(6);
  gfx->setTextColor(COL_TEXT, COL_BG);

  String sPrice =
    isnan(cr_price)
      ? "--.--"
      : (g_fiat + " " + crFmtFiat(cr_price));

  int tw = sPrice.length() * BASE_CHAR_W * 6;
  gfx->setCursor((480 - tw) / 2, y + 70);
  gfx->print(sPrice);

  // --------------------------------------------------------------------------
  // BLOCCO 2 — variazione 24h
  // --------------------------------------------------------------------------
  gfx->setTextSize(3);

  String s24 = "--%";
  uint16_t col = COL_TEXT;

  if (!isnan(cr_chg24)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f%%", cr_chg24);
    s24 = buf;
    col = (cr_chg24 >= 0.0) ? 0x0000 : 0x9000;  // mantengo i tuoi colori custom
  }

  gfx->setTextColor(col, COL_BG);
  int wtxt = s24.length() * BASE_CHAR_W * 3;
  gfx->setCursor((480 - wtxt) / 2, y + 150);
  gfx->print(s24);

  // --------------------------------------------------------------------------
  // BLOCCO 3 — valore totale BTC posseduti (se definito)
  // --------------------------------------------------------------------------
  if (!isnan(g_btc_owned) && !isnan(cr_price)) {

    gfx->setTextSize(3);

    String L1 = crFmtBTC(g_btc_owned) + " BTC";
    int w1 = L1.length() * BASE_CHAR_W * 3;
    gfx->setCursor((480 - w1) / 2, y + 240);
    gfx->print(L1);

    double tot = g_btc_owned * cr_price;
    String L2 = "= " + g_fiat + " " + crFmtFiat(tot);
    int w2 = L2.length() * BASE_CHAR_W * 3;
    gfx->setCursor((480 - w2) / 2, y + 280);
    gfx->print(L2);
  }

  // --------------------------------------------------------------------------
  // FOOTER — tempo dall’ultimo aggiornamento
  // --------------------------------------------------------------------------
  gfx->setTextSize(2);
  int fy = y + 340;
  drawHLine(fy);
  fy += 12;

  String sub = it
    ? "Fonte: CoinGecko | Aggiornato "
    : "Source: CoinGecko | Updated ";

  if (cr_last_update_ms) {
    unsigned long s = (millis() - cr_last_update_ms) / 1000;
    sub += String(s) + (it ? "s fa" : "s ago");
  } else {
    sub += (it ? "n/d" : "n/a");
  }

  drawBoldMain(PAGE_X, fy + CHAR_H, sub, TEXT_SCALE);

  gfx->setTextSize(TEXT_SCALE);
}

// ============================================================================
// WRAPPERS — usati dal main
// ============================================================================
inline bool fetchCryptoWrapper() { return fetchCrypto(); }
inline void pageCryptoWrapper()  { pageCrypto(); }

