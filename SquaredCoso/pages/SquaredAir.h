#pragma once
/*
===============================================================================
   SQUARED — PAGINA “AIR QUALITY” (Hyper-Optimized v2)
   - Nessun lambda
   - Tabelle testo in PROGMEM
   - Buffer locali piccoli e riusati
   - Particle system con startWrite/endWrite (DMA-friendly)
===============================================================================
*/

#include <Arduino.h>
#include "../handlers/globals.h"

// ============================================================================
// EXTERN
// ============================================================================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_HEADER;
extern const uint16_t COL_TEXT;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern void   drawHeader(const String& title);
extern void   drawBoldMain(int16_t x, int16_t y, const String&, uint8_t scale);
extern void   drawHLine(int y);
extern String sanitizeText(const String&);
extern int    indexOfCI(const String&, const String&, int from);
extern bool   httpGET(const String& url, String& out, uint32_t timeout);
extern bool   geocodeIfNeeded();

extern String g_city;
extern String g_lang;

// colore usato anche da fuori per FX globali
extern uint16_t g_air_bg;

// ============================================================================
// COLORI SFONDO
// ============================================================================
#define AIR_BG_GOOD       0x232E
#define AIR_BG_FAIR       0xA7F0
#define AIR_BG_MODERATE   0xFFE0
#define AIR_BG_POOR       0xFD20
#define AIR_BG_VERYPOOR   0xF800

// ============================================================================
// CACHE valori
// ============================================================================
static float aq_pm25 = NAN;
static float aq_pm10 = NAN;
static float aq_o3   = NAN;
static float aq_no2  = NAN;


// ============================================================================
// JSON EXTRACTION (HYPER-OPTIMIZED)
// ============================================================================
static bool extractObjectBlock(const String& body,
                               const char* key,
                               String& out)
{
  String k = "\"";
  k += key;
  k += '"';

  int p = indexOfCI(body, k, 0);
  if (p < 0) return false;

  int b = body.indexOf('{', p);
  if (b < 0) return false;

  int depth = 0;
  const int len = body.length();
  for (int i = b; i < len; i++) {
    char c = body[i];
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        out = body.substring(b, i + 1);
        return true;
      }
    }
  }
  return false;
}

static float parseFirstNumber(const String& obj, const char* key)
{
  String k = "\"";
  k += key;
  k += '"';

  int p = indexOfCI(obj, k, 0);
  if (p < 0) return NAN;

  int a = obj.indexOf('[', p);
  if (a < 0) return NAN;

  int s = a + 1;
  const int len = obj.length();
  while (s < len && isspace(obj[s])) s++;

  if (s >= len || obj[s] == '"') return NAN;

  int e = s;
  while (e < len && obj[e] != ',' && obj[e] != ']') e++;

  return obj.substring(s, e).toFloat();
}


// ============================================================================
// CLASSIFICAZIONE — TESTI IN PROGMEM
// ============================================================================
static int catFrom(const float v,
                   const float a,
                   const float b,
                   const float c,
                   const float d)
{
  if (isnan(v)) return -1;
  if (v <= a) return 0;
  if (v <= b) return 1;
  if (v <= c) return 2;
  if (v <= d) return 3;
  return 4;
}

// categorie / consigli (IT / EN) in PROGMEM
static const char cat_it[][20] PROGMEM = {
  "Aria buona",
  "Aria discreta",
  "Aria moderata",
  "Aria scadente",
  "Aria pessima"
};
static const char cat_en[][20] PROGMEM = {
  "Good air",
  "Fair air",
  "Moderate air",
  "Poor air",
  "Very poor air"
};

static const char tip_it[][40] PROGMEM = {
  "tutto ok!",
  "nessuna precauzione.",
  "sensibili: limitare sforzo.",
  "evitare attivita fuori!",
  "rimanere al chiuso!"
};
static const char tip_en[][40] PROGMEM = {
  "all good!",
  "no precautions needed.",
  "sensitive: reduce effort.",
  "avoid long outdoor activity!",
  "stay indoors!"
};

// costruisce la frase finale in buffer, ritorna String per compatibilita
static String airVerdict(int& categoryOut)
{
  int worst = -1;

  worst = max(worst, catFrom(aq_pm25, 10, 20, 25, 50));
  worst = max(worst, catFrom(aq_pm10, 20, 40, 50, 100));
  worst = max(worst, catFrom(aq_o3,   80, 120, 180, 240));
  worst = max(worst, catFrom(aq_no2,  40, 90, 120, 230));

  categoryOut = worst;

  if (worst < 0) {
    if (g_lang == "it")
      return String("Aria: dati non disponibili");
    return String("Air: data unavailable");
  }

  char buffer1[20];
  char buffer2[40];

  if (g_lang == "it") {
    strcpy_P(buffer1, cat_it[worst]);
    strcpy_P(buffer2, tip_it[worst]);
  } else {
    strcpy_P(buffer1, cat_en[worst]);
    strcpy_P(buffer2, tip_en[worst]);
  }

  String s(buffer1);
  s += ", ";
  s += buffer2;
  return s;
}


// ============================================================================
// FETCH AIR QUALITY
// ============================================================================
static bool fetchAir()
{
  if (!geocodeIfNeeded()) return false;

  String url =
    "https://air-quality-api.open-meteo.com/v1/air-quality?"
    "latitude="  + g_lat +
    "&longitude=" + g_lon +
    "&hourly=pm2_5,pm10,ozone,nitrogen_dioxide"
    "&timezone=auto";

  String body;
  if (!httpGET(url, body, 10000)) return false;

  String hourlyBlk;
  if (!extractObjectBlock(body, "hourly", hourlyBlk)) return false;

  aq_pm25 = parseFirstNumber(hourlyBlk, "pm2_5");
  aq_pm10 = parseFirstNumber(hourlyBlk, "pm10");
  aq_o3   = parseFirstNumber(hourlyBlk, "ozone");
  aq_no2  = parseFirstNumber(hourlyBlk, "nitrogen_dioxide");

  return true;
}


// ============================================================================
// PARTICLE SYSTEM — FOGLIE (DMA-friendly)
// ============================================================================
#define N_LEAVES 45  // compromesso CPU / resa

struct Leaf {
  float    x, y;
  float    baseY;
  float    phase;
  float    vx;
  float    amp;
  uint16_t color;
  int16_t  oldX, oldY;
  uint8_t  size;
};

static Leaf     leaves[N_LEAVES];
static bool     leavesInit   = false;
static uint32_t leavesLastMs = 0;

// due colori in PROGMEM
static const uint16_t LEAF_COLORS[2] PROGMEM = { 0xFD80, 0xA440 };

// init singola foglia
static inline void initLeafFX(const int i)
{
  Leaf &L = leaves[i];

  L.x      = (float)random(-40, -10);
  L.y      = (float)random(310, 470);
  L.baseY  = L.y;

  L.vx     = 1.0f + (float)(random(0, 20)) * 0.1f;
  L.amp    = (float)(6 + random(0, 10));
  L.phase  = (float)(random(0, 628)) * 0.01f;

  L.size   = (uint8_t)random(0, 3);
  L.color  = pgm_read_word(&LEAF_COLORS[random(0, 2)]);

  L.oldX   = -1;
  L.oldY   = -1;
}

// disegna una foglia (piccola croce/pixel cluster)
static inline void drawLeafFX(const int x,
                              const int y,
                              const uint8_t s,
                              const uint16_t c)
{
  gfx->drawPixel(x, y, c);
  if (s > 0) {
    gfx->drawPixel(x - 1, y, c);
    gfx->drawPixel(x + 1, y, c);
  }
  if (s > 1) {
    gfx->drawPixel(x, y - 1, c);
    gfx->drawPixel(x, y + 1, c);
    gfx->drawPixel(x - 1, y - 1, c);
    gfx->drawPixel(x + 1, y - 1, c);
    gfx->drawPixel(x - 1, y + 1, c);
    gfx->drawPixel(x + 1, y + 1, c);
  }
}

// tick animazione foglie — DMA-friendly (startWrite/endWrite)
static void tickLeaves(const uint16_t bg)
{
  const uint32_t now = millis();
  if (now - leavesLastMs < 33U) return;   // ~30 FPS
  leavesLastMs = now;

  if (!leavesInit) {
    leavesInit = true;
    for (int i = 0; i < N_LEAVES; i++)
      initLeafFX(i);
  }

  gfx->startWrite();  // blocco unico di scrittura

  for (int i = 0; i < N_LEAVES; i++) {

    Leaf &L = leaves[i];

    // cancella stato precedente (solo se in basso, come prima)
    if (L.oldY >= 300) {
      drawLeafFX(L.oldX, L.oldY, L.size, bg);
    }

    // aggiornamento posizione
    L.x     += L.vx;
    L.phase += 0.03f;
    L.y      = L.baseY + sinf(L.phase) * L.amp;

    // respawn fuori dallo schermo destro
    if (L.x > 500.0f) {
      initLeafFX(i);
      continue;
    }

    const int ix = (int)L.x;
    const int iy = (int)L.y;

    if (iy >= 300 && iy < 480 && ix >= -2 && ix < 482) {
      drawLeafFX(ix, iy, L.size, L.color);
    }

    L.oldX = ix;
    L.oldY = iy;
  }

  gfx->endWrite();
}


// ============================================================================
// ROW HELPERS (niente lambda)
// ============================================================================
static void airRowPrint(const char* lbl,
                        float value,
                        int& y,
                        uint16_t bg)
{
  const uint8_t SZ = 3;

  gfx->setTextSize(SZ);
  gfx->setTextColor(COL_TEXT, bg);

  gfx->setCursor(PAGE_X, y);
  gfx->print(lbl);

  char buf[16];

  if (isnan(value)) {
    strcpy(buf, "--");
  } else {
    // PM2.5 → 1 decimale, il resto intero (come prima)
    bool pm25 = (lbl[2] == '2' && lbl[3] == '5');
    dtostrf(value, 0, pm25 ? 1 : 0, buf);
  }

  const char* unit = (g_lang == "it") ? " ug/m3" : " \xC2\xB5""g/m\xC2\xB3";
  // NB: "µ" e "³" in UTF-8, come prima visualmente sul pannello

  char out[24];
  snprintf(out, sizeof(out), "%s%s", buf, unit);

  const int maxW   = 480 - PAGE_X * 2;
  const int textW  = (int)strlen(out) * BASE_CHAR_W * SZ;
  int xr = 480 - PAGE_X - textW;
  if (xr < PAGE_X + 120 && textW < maxW)
    xr = PAGE_X + 120;

  gfx->setCursor(xr, y);
  gfx->print(out);

  y += BASE_CHAR_H * SZ + 10;
}


// ============================================================================
// PAGE AIR QUALITY
// ============================================================================
static void pageAir()
{
  const bool it = (g_lang == "it");

  drawHeader(it
    ? String("Aria a ") + sanitizeText(g_city)
    : String("Air quality \xE2\x80\x93 ") + sanitizeText(g_city));

  // classificazione + testo
  int    cat     = -1;
  String verdict = airVerdict(cat);

  uint16_t bg = COL_BG;
  switch (cat) {
    case 0: bg = AIR_BG_GOOD;     break;
    case 1: bg = AIR_BG_FAIR;     break;
    case 2: bg = AIR_BG_MODERATE; break;
    case 3: bg = AIR_BG_POOR;     break;
    case 4: bg = AIR_BG_VERYPOOR; break;
  }
  g_air_bg = bg;

  // sfondo pagina
  gfx->fillRect(0, PAGE_Y, 480, 480 - PAGE_Y, bg);

  int y = PAGE_Y + 14;

  // righe valori
  airRowPrint("PM2.5:", aq_pm25, y, bg);
  airRowPrint("PM10 :", aq_pm10, y, bg);
  airRowPrint(it ? "OZONO:" : "OZONE", aq_o3, y, bg);
  airRowPrint("NO2  :", aq_no2, y, bg);

  // separatore
  drawHLine(y);
  y += 14;

  // testo "verdetto" con word-wrap centrale
  gfx->setTextSize(TEXT_SCALE + 1);
  gfx->setTextColor(COL_TEXT, bg);

  const int maxW = 480 - PAGE_X * 2;
  int maxC = maxW / (BASE_CHAR_W * (TEXT_SCALE + 1));
  if (maxC < 10) maxC = 10;

  String s = sanitizeText(verdict);
  s.trim();

  int start = 0;
  const int slen = s.length();
  y += 12;

  while (start < slen) {
    int len = slen - start;
    if (len > maxC) len = maxC;

    int cut = start + len;

    if (cut < slen) {
      int sp = s.lastIndexOf(' ', cut);
      if (sp > start) cut = sp;
    }

    String line = s.substring(start, cut);
    line.trim();

    const int lineLen = line.length();
    if (lineLen > 0) {
      const int w  = lineLen * BASE_CHAR_W * (TEXT_SCALE + 1);
      int x = (480 - w) / 2;
      if (x < PAGE_X) x = PAGE_X;

      gfx->setCursor(x, y);
      gfx->print(line);

      y += BASE_CHAR_H * (TEXT_SCALE + 1) + 6;
    }

    if (cut >= slen) break;
    start = (s.charAt(cut) == ' ') ? (cut + 1) : cut;

    if (y > 420) break;
  }

  gfx->setTextSize(TEXT_SCALE);

  // FX foglie (usa bg locale)
  tickLeaves(bg);
}

