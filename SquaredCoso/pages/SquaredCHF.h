#pragma once

#include <Arduino.h>
#include "../handlers/globals.h"

/*
===============================================================================
   SQUARED — MODULO FX (Valute + Money Rain) — Versione definitiva

   - Spawn PERFETTO: subito sotto header, a Y = 80.
   - Movimento: discesa verticale + oscillazione sinusoidale + rotazione.
   - Nessun accumulo sul fondo: quando il bordo inferiore supera AREA_Y_MAX,
     la banconota NON viene disegnata e viene respawnata dall’alto.
   - Clamp laterale usando il raggio (semi-diagonale) → non tocca mai i bordi.
   - Ottimizzato per ESP32-S3: nessun alloc dinamico, nessuna String inutile.
===============================================================================
*/

// ============================================================================
// EXTERN
// ============================================================================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_TEXT;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern String g_fiat;

extern void   drawHeader(const String& title);
extern String sanitizeText(const String&);
extern bool   httpGET(const String&, String&, uint32_t);

// ============================================================================
// FX GLOBALS (valute)
// ============================================================================
double fx_eur = NAN, fx_usd = NAN, fx_gbp = NAN, fx_jpy = NAN;
double fx_cad = NAN, fx_cny = NAN, fx_inr = NAN, fx_chf = NAN;

double fx_prev_eur = NAN, fx_prev_usd = NAN, fx_prev_gbp = NAN;
double fx_prev_jpy = NAN, fx_prev_cad = NAN;
double fx_prev_cny = NAN, fx_prev_inr = NAN, fx_prev_chf = NAN;

// ============================================================================
// FORMATTER
// ============================================================================
static inline const char* fmtDoubleBuf(double v, uint8_t dec, char* out)
{
  dtostrf(v, 0, dec, out);
  return out;
}

// ============================================================================
// PARSER JSON
// ============================================================================
static double parseJsonRate(const char* b, const char* code)
{
  char key[12];
  snprintf(key, sizeof(key), "\"%s\":", code);

  const char* p = strstr(b, key);
  if (!p) return NAN;
  p += strlen(key);

  bool neg = false;
  if (*p == '-') { neg = true; p++; }

  double val = 0;
  while (*p >= '0' && *p <= '9') val = val * 10 + (*p++ - '0');

  if (*p == '.') {
    p++;
    double frac = 0, div = 1;
    while (*p >= '0' && *p <= '9') {
      frac = frac * 10 + (*p++ - '0');
      div *= 10;
    }
    val += frac / div;
  }

  return neg ? -val : val;
}

// ============================================================================
// FETCH FX
// ============================================================================
bool fetchFX()
{
  if (!g_fiat.length()) g_fiat = "CHF";

  String body;
  if (!httpGET(
        "https://api.frankfurter.app/latest?from=" + g_fiat +
        "&to=CHF,EUR,USD,GBP,JPY,CAD,CNY,INR",
        body, 10000))
    return false;

  const char* b = body.c_str();

  fx_eur = parseJsonRate(b, "EUR");
  fx_usd = parseJsonRate(b, "USD");
  fx_gbp = parseJsonRate(b, "GBP");
  fx_jpy = parseJsonRate(b, "JPY");
  fx_cad = parseJsonRate(b, "CAD");
  fx_cny = parseJsonRate(b, "CNY");
  fx_inr = parseJsonRate(b, "INR");
  fx_chf = parseJsonRate(b, "CHF");

  return true;
}

// ============================================================================
// MONEY RAIN
// ============================================================================

#define MONEY_COUNT  22

// zona animazione (header termina a y = 80)
#define AREA_X_MIN   260
#define AREA_X_MAX   479
#define AREA_Y_MIN   80
#define AREA_Y_MAX   479

#define SAFE_M       3

struct MoneyDrop {
  float x, y;
  float vy;
  float phase, amp;
  float rot, drot;
  uint8_t size;
  uint16_t color;
};

static MoneyDrop money[MONEY_COUNT];
static bool      moneyInit = false;
static uint32_t  moneyLast = 0;

static const uint16_t MONEY_COL[2] = { 0x07E0, 0x03E0 };


// ============================================================================
// RAGGIO (semi-diagonale)
// ============================================================================
static inline int moneyRadius(uint8_t s)
{
  const int w = 6 + s * 2;
  const int h = 8 + s * 2;
  float diag = sqrtf(float(w*w + h*h));
  return int(diag * 0.5f) + SAFE_M;
}


// ============================================================================
// DRAW MONEY
// ============================================================================
static inline void drawMoney(int cx, int cy, uint8_t s, float rot, uint16_t col)
{
  const int w  = 6 + s * 2;
  const int h  = 8 + s * 2;
  const int hw = w >> 1;
  const int hh = h >> 1;

  float cs = cosf(rot);
  float sn = sinf(rot);

  for (int dy = -hh; dy <= hh; dy++) {

    float ryx = dy * sn;
    float ryy = dy * cs;

    for (int dx = -hw; dx <= hw; dx++) {

      int px = cx + int(dx * cs - ryx);
      int py = cy + int(dx * sn + ryy);

      if (px < AREA_X_MIN || px > AREA_X_MAX) continue;
      if (py < AREA_Y_MIN || py > AREA_Y_MAX) continue;

      gfx->drawPixel(px, py, col);
    }
  }
}


// ============================================================================
// INIT BANCONOTA — spawn correttamente sotto l’header (y=80)
// ============================================================================
static inline void moneyInitOne(int i)
{
  MoneyDrop &m = money[i];

  m.size = uint8_t(random(0, 3));
  int r  = moneyRadius(m.size);

  // Spawn orizzontale libero
  m.x = random(AREA_X_MIN + r, AREA_X_MAX - r);

  // Spawn verticale preciso: parte SOPRA l’area visibile
  m.y = AREA_Y_MIN - random(40, 140) - r;

  // Movimento
  m.vy    = 1.2f + random(0, 14) * 0.08f;
  m.amp   = 3 + random(0, 6);
  m.phase = random(0, 628) * 0.01f;

  m.rot  = random(0, 628) * 0.01f;
  m.drot = 0.03f + random(0, 15) * 0.01f;

  m.color = MONEY_COL[random(0, 2)];
}


// ============================================================================
// TICK ANIMAZIONE
// ============================================================================
void tickFXDataStream(uint16_t bg)
{
  uint32_t now = millis();
  if (now - moneyLast < 33) return;
  moneyLast = now;

  if (!moneyInit) {
    moneyInit = true;
    for (int i = 0; i < MONEY_COUNT; i++)
      moneyInitOne(i);
  }

  for (int i = 0; i < MONEY_COUNT; i++) {

    MoneyDrop &m = money[i];
    int r = moneyRadius(m.size);

    drawMoney(int(m.x), int(m.y), m.size, m.rot, bg);

    m.phase += 0.035f;
    float dx = sinf(m.phase) * m.amp;

    m.y   += m.vy;
    m.rot += m.drot;

    float nx = m.x + dx;
    float ny = m.y;

    // oltre il fondo → respawn invisibile
    if (ny - r > AREA_Y_MAX) {
      moneyInitOne(i);
      continue;
    }

    // clamp laterale
    if (nx < AREA_X_MIN + r) nx = AREA_X_MIN + r;
    if (nx > AREA_X_MAX - r) nx = AREA_X_MAX - r;

    // non risalire sopra header
    if (ny < AREA_Y_MIN - r) ny = AREA_Y_MIN - r;

    drawMoney(int(nx), int(ny), m.size, m.rot, m.color);

    m.x = nx;
    m.y = ny;
  }
}


// ============================================================================
// PAGE FX
// ============================================================================
void pageFX()
{
  drawHeader("Valute vs " + g_fiat);

  gfx->fillRect(AREA_X_MIN, AREA_Y_MIN,
                AREA_X_MAX - AREA_X_MIN + 1,
                AREA_Y_MAX - AREA_Y_MIN + 1,
                COL_BG);

  int y = PAGE_Y;
  const uint8_t scale = TEXT_SCALE + 1;

  const uint16_t COL_UP   = 0x0340;
  const uint16_t COL_DOWN = 0x9000;
  const uint16_t COL_NEUT = COL_TEXT;

  gfx->setTextSize(scale);
  char buf[32];

  auto printRow = [&](const char* lbl, double prev, double val, uint8_t dec)
  {
    gfx->setCursor(PAGE_X, y + CHAR_H);
    gfx->setTextColor(COL_TEXT, COL_BG);
    gfx->print(lbl);

    uint16_t col = COL_NEUT;
    if (!isnan(prev) && !isnan(val)) {
      if (val > prev)      col = COL_UP;
      else if (val < prev) col = COL_DOWN;
    }

    gfx->setTextColor(col, COL_BG);
    gfx->setCursor(PAGE_X + 70, y + CHAR_H);

    if (isnan(val)) gfx->print("--");
    else gfx->print(fmtDoubleBuf(val, dec, buf));

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
}

