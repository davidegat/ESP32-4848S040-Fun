#pragma once

#include <Arduino.h>
#include "../handlers/globals.h"

// ============================================================================
//  MODULO: VALUTE (Frankfurter API) + ANIMAZIONE SOLDI ROTANTI
// ============================================================================

// ======================
// EXTERN DAL MAIN
// ======================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_HEADER;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_GOOD;
extern const uint16_t COL_BAD;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern String g_fiat;

extern void drawHeader(const String& title);
extern String sanitizeText(const String&);
extern bool httpGET(const String&, String&, uint32_t);


// ============================================================================
// ============================= FX DATA ======================================
// ============================================================================
double fx_eur = NAN, fx_usd = NAN, fx_gbp = NAN;
double fx_jpy = NAN, fx_cad = NAN, fx_cny = NAN;
double fx_inr = NAN, fx_chf = NAN;

double fx_prev_eur = NAN, fx_prev_usd = NAN, fx_prev_gbp = NAN;
double fx_prev_jpy = NAN, fx_prev_cad = NAN;
double fx_prev_cny = NAN, fx_prev_inr = NAN;
double fx_prev_chf = NAN;


// ============================================================================
// FORMATTORE
// ============================================================================
static inline String fmtDouble(double v, unsigned int dec) {
  char buf[32];
  dtostrf(v, 0, dec, buf);
  return String(buf);
}


// ============================================================================
// PARSER JSON
// ============================================================================
static inline double getRate(const String& body, const char* code) {

  char key[16];
  snprintf(key, sizeof(key), "\"%s\":", code);

  int p = body.indexOf(key);
  if (p < 0) return NAN;

  int s = p + strlen(key);
  int e = s;
  const int L = body.length();

  while (e < L) {
    char c = body[e];
    if (!(c == '-' || c == '.' || (c >= '0' && c <= '9')))
      break;
    e++;
  }

  return body.substring(s, e).toDouble();
}


// ============================================================================
// FETCH FX  (PUBBLICA - NON STATIC !)
// ============================================================================
bool fetchFX() {

  if (g_fiat.length() == 0)
    g_fiat = "CHF";

  String url =
    "https://api.frankfurter.app/latest?from=" +
    g_fiat +
    "&to=CHF,EUR,USD,GBP,JPY,CAD,CNY,INR";

  String body;
  if (!httpGET(url, body, 10000))
    return false;

  fx_eur = getRate(body, "EUR");
  fx_usd = getRate(body, "USD");
  fx_gbp = getRate(body, "GBP");
  fx_jpy = getRate(body, "JPY");
  fx_cad = getRate(body, "CAD");
  fx_cny = getRate(body, "CNY");
  fx_inr = getRate(body, "INR");
  fx_chf = getRate(body, "CHF");

  return true;
}



// ============================================================================
// ===================== ANIMAZIONE SOLDI ROTANTI (NUOVA VERSIONE) ============
// ============================================================================
// Soldi più larghi, area allargata fino a ~metà schermo (260–479),
// nessuna collisione con i bordi e rotazione continua.

#define MONEY_COUNT 28

struct MoneyDrop {
  float x, y;
  float vy;
  float phase;
  float amp;
  float speed;
  float rot;
  float drot;
  uint8_t size;
  uint16_t color;
};

static MoneyDrop money[MONEY_COUNT];
static bool moneyInit = false;
static uint32_t moneyLast = 0;

// area di animazione (rettangolo allargato)
static const int FX_MIN_X = 260;  // prima era 320
static const int FX_MAX_X = 479;

static const uint16_t MONEY_COL[2] = {
  0x07E0,
  0x03E0
};


// ---------------------------------------------------------------------------
// Rettangolo ruotato potente (più grande)
// ---------------------------------------------------------------------------
static void drawRotMoney(int cx, int cy, uint8_t s, float angle, uint16_t col)
{
  int w = 6 + s * 3;   // large = 12px di larghezza
  int h = 10 + s * 4;  // large = 18px di altezza

  float c = cos(angle);
  float si = sin(angle);

  for (int dy = -h/2; dy <= h/2; dy++) {
    for (int dx = -w/2; dx <= w/2; dx++) {

      int rx = (int)(dx * c - dy * si);
      int ry = (int)(dx * si + dy * c);

      int px = cx + rx;
      int py = cy + ry;

      if (py < PAGE_Y) continue; // NON toccare header

      // solo dentro il rettangolo animazione
      if (px >= FX_MIN_X && px <= FX_MAX_X && py >= PAGE_Y && py < 480)
        gfx->drawPixel(px, py, col);
    }
  }
}


// ---------------------------------------------------------------------------
// inizializza un soldo
// ---------------------------------------------------------------------------
static void initMoney(int i)
{
  money[i].x = random(FX_MIN_X + 10, FX_MAX_X - 10);
  money[i].y = random(-150, -20);

  money[i].vy = 1.6f + random(0, 25) / 10.0f;

  money[i].amp   = 5 + random(0, 6);
  money[i].speed = 0.013f + random(0, 20) / 500.0f;
  money[i].phase = random(0, 628) / 100.0f;

  money[i].rot  = random(0, 628) / 100.0f;
  money[i].drot = 0.06f + random(0, 30) / 200.0f;

  money[i].size  = random(0, 3);
  money[i].color = MONEY_COL[random(0, 2)];
}


// ---------------------------------------------------------------------------
// tickFXDataStream – mantiene il nome originale
// ---------------------------------------------------------------------------
void tickFXDataStream(uint16_t bg)
{
  uint32_t now = millis();
  if (now - moneyLast < 33) return;
  moneyLast = now;

  if (!moneyInit) {
    moneyInit = true;
    for (int i = 0; i < MONEY_COUNT; i++)
      initMoney(i);
  }

  for (int i = 0; i < MONEY_COUNT; i++)
  {
    // cancellazione
    drawRotMoney((int)money[i].x, (int)money[i].y, money[i].size, money[i].rot, bg);

    // oscillazione laterale
    money[i].phase += money[i].speed;
    float dx = sin(money[i].phase * 6.28f) * money[i].amp;

    money[i].y += money[i].vy;
    money[i].rot += money[i].drot;

    float nx = money[i].x + dx;
    float ny = money[i].y;

    // -----------------------------------------------------
    // EVITA collisioni con bordi del rettangolo
    // -----------------------------------------------------
    int margin = 8 + money[i].size * 2;  // margine per rettangoli grandi

    if (nx < FX_MIN_X + margin) nx = FX_MIN_X + margin;
    if (nx > FX_MAX_X - margin) nx = FX_MAX_X - margin;

    // -----------------------------------------------------
    // reinizializza quando esce in basso
    // -----------------------------------------------------
    if (ny > 500) {
      initMoney(i);
      continue;
    }

    // nuovo disegno
    drawRotMoney((int)nx, (int)ny, money[i].size, money[i].rot, money[i].color);

    money[i].x = nx;
    money[i].y = ny;
  }
}




// ============================================================================
// ============================ PAGE FX =======================================
// ============================================================================
void pageFX() {

  String hdr = "Valute vs ";
  hdr += g_fiat;
  drawHeader(hdr);

  int y = PAGE_Y;
  const uint8_t scale = TEXT_SCALE + 1;

  const uint16_t COL_UP   = 0x0340;
  const uint16_t COL_DOWN = 0x9000;
  const uint16_t COL_NEUT = COL_TEXT;

  gfx->setTextSize(scale);

  const char* labels[8] = {
    "CHF:", "EUR:", "USD:", "GBP:", "JPY:", "CAD:", "CNY:", "INR:"
  };

  int maxLabelW = 0;
  for (int i = 0; i < 8; i++) {
    int w = strlen(labels[i]) * (6 * scale);
    if (w > maxLabelW) maxLabelW = w;
  }

  const int valueX = PAGE_X + maxLabelW + 16;

  auto printRow = [&](const char* lbl, double prevVal, double val, uint8_t decimals)
  {
    gfx->setCursor(PAGE_X, y + CHAR_H);
    gfx->setTextColor(COL_TEXT, COL_BG);
    gfx->print(lbl);

    uint16_t col = COL_NEUT;
    if (!isnan(prevVal) && !isnan(val)) {
      if (val > prevVal) col = COL_UP;
      else if (val < prevVal) col = COL_DOWN;
    }

    gfx->setCursor(valueX, y + CHAR_H);
    gfx->setTextColor(col, COL_BG);

    if (isnan(val)) gfx->print("--");
    else gfx->print(fmtDouble(val, decimals));

    y += CHAR_H * scale + 6;
  };

  if (g_fiat != "CHF") printRow("CHF:", fx_prev_chf, fx_chf, 3);
  if (g_fiat != "EUR") printRow("EUR:", fx_prev_eur, fx_eur, 3);
  if (g_fiat != "USD") printRow("USD:", fx_prev_usd, fx_usd, 3);
  if (g_fiat != "GBP") printRow("GBP:", fx_prev_gbp, fx_gbp, 3);
  if (g_fiat != "JPY") printRow("JPY:", fx_prev_jpy, fx_jpy, 1);
  if (g_fiat != "CAD") printRow("CAD:", fx_prev_cad, fx_cad, 3);
  if (g_fiat != "CNY") printRow("CNY:", fx_prev_cny, fx_cny, 3);
  if (g_fiat != "INR") printRow("INR:", fx_prev_inr, fx_inr, 3);

  fx_prev_chf = fx_chf;
  fx_prev_eur = fx_eur;
  fx_prev_usd = fx_usd;
  fx_prev_gbp = fx_gbp;
  fx_prev_jpy = fx_jpy;
  fx_prev_cad = fx_cad;
  fx_prev_cny = fx_cny;
  fx_prev_inr = fx_inr;

  // pulizia area animazione
  gfx->fillRect(320, PAGE_Y, 160, 480 - PAGE_Y, COL_BG);
}

