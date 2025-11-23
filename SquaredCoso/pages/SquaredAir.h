#pragma once
/*
===============================================================================
   SQUARED — PAGINA “AIR QUALITY” (Hyper-Optimized)
   - Eliminato quasi tutto l’uso di String (tranne ciò che serve per API esterne)
   - Tabelle in PROGMEM
   - Ridotto parsing, rimosse copie e substring
   - Particle system alleggerito
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

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String&, uint8_t scale);
extern void drawHLine(int y);
extern String sanitizeText(const String&);
extern int indexOfCI(const String&, const String&, int from);
extern bool httpGET(const String& url, String& out, uint32_t timeout);
extern bool geocodeIfNeeded();

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
  const String k = String("\"") + key + "\"";
  int p = indexOfCI(body, k, 0);
  if (p < 0) return false;

  int b = body.indexOf('{', p);
  if (b < 0) return false;

  int depth = 0;
  int len = body.length();
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
  const String k = String("\"") + key + "\"";
  int p = indexOfCI(obj, k, 0);
  if (p < 0) return NAN;

  int a = obj.indexOf('[', p);
  if (a < 0) return NAN;

  int s = a + 1;
  while (s < obj.length() && isspace(obj[s])) s++;

  if (obj[s] == '"') return NAN;

  int e = s;
  while (e < obj.length() && obj[e] != ',' && obj[e] != ']') e++;

  return obj.substring(s, e).toFloat();
}


// ============================================================================
// CLASSIFICAZIONE (Compattata)
// ============================================================================
static int catFrom(const float v, const float a, const float b, const float c, const float d)
{
  if (isnan(v)) return -1;
  if (v <= a) return 0;
  if (v <= b) return 1;
  if (v <= c) return 2;
  if (v <= d) return 3;
  return 4;
}

static String airVerdict(int& categoryOut)
{
  int worst = -1;

  worst = max(worst, catFrom(aq_pm25, 10, 20, 25, 50));
  worst = max(worst, catFrom(aq_pm10, 20, 40, 50, 100));
  worst = max(worst, catFrom(aq_o3,   80, 120, 180, 240));
  worst = max(worst, catFrom(aq_no2,  40, 90, 120, 230));

  categoryOut = worst;

  if (worst < 0)
    return (g_lang == "it")
            ? "Aria: dati non disponibili"
            : "Air: data unavailable";

  static const char cat_it[][20] PROGMEM = {
    "Aria buona", "Aria discreta", "Aria moderata",
    "Aria scadente", "Aria pessima"
  };
  static const char cat_en[][20] PROGMEM = {
    "Good air", "Fair air", "Moderate air",
    "Poor air", "Very poor air"
  };

  static const char tip_it[][40] PROGMEM = {
    "tutto ok!", "nessuna precauzione.",
    "sensibili: limitare sforzo.", "evitare attività fuori!",
    "rimanere al chiuso!"
  };
  static const char tip_en[][40] PROGMEM = {
    "all good!", "no precautions needed.",
    "sensitive: reduce effort.", "avoid long outdoor activity!",
    "stay indoors!"
  };

  char buffer1[40], buffer2[40];

  if (g_lang == "it") {
    strcpy_P(buffer1, cat_it[worst]);
    strcpy_P(buffer2, tip_it[worst]);
  } else {
    strcpy_P(buffer1, cat_en[worst]);
    strcpy_P(buffer2, tip_en[worst]);
  }

  String s = buffer1;
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
    "latitude=" + g_lat +
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
// PARTICLE SYSTEM — FOGLIE (HYPER-OPTIMIZED)
// ============================================================================
#define N_LEAVES 45  // ridotto da 50 (meno CPU)

struct Leaf {
  float x, y;
  float baseY;
  float phase;
  float vx;
  float amp;
  uint16_t color;
  int16_t oldX, oldY;
  uint8_t size;
};

static Leaf leaves[N_LEAVES];
static bool leavesInit = false;
static uint32_t leavesLastMs = 0;

static const uint16_t LEAF_COLORS[2] PROGMEM = { 0xFD80, 0xA440 };

static inline void initLeafFX(const int i)
{
  leaves[i].x = random(-40, -10);
  leaves[i].y = random(310, 470);
  leaves[i].baseY = leaves[i].y;

  leaves[i].vx    = 1.0f + (random(0, 20) * 0.1f);
  leaves[i].amp   = 6 + random(0, 10);
  leaves[i].phase = random(0, 628) * 0.01f;

  leaves[i].size  = random(0, 3);
  leaves[i].color = pgm_read_word(&LEAF_COLORS[random(0, 2)]);

  leaves[i].oldX = -1;
  leaves[i].oldY = -1;
}

static inline void drawLeafFX(const int x, const int y, const uint8_t s, const uint16_t c)
{
  gfx->drawPixel(x, y, c);
  if (s > 0) { gfx->drawPixel(x-1,y,c); gfx->drawPixel(x+1,y,c); }
  if (s > 1) {
    gfx->drawPixel(x,y-1,c); gfx->drawPixel(x,y+1,c);
    gfx->drawPixel(x-1,y-1,c); gfx->drawPixel(x+1,y-1,c);
    gfx->drawPixel(x-1,y+1,c); gfx->drawPixel(x+1,y+1,c);
  }
}

void tickLeaves(const uint16_t bg)
{
  uint32_t now = millis();
  if (now - leavesLastMs < 33) return;
  leavesLastMs = now;

  if (!leavesInit) {
    leavesInit = true;
    for (int i = 0; i < N_LEAVES; i++)
      initLeafFX(i);
  }

  for (int i = 0; i < N_LEAVES; i++) {

    if (leaves[i].oldY >= 300)
      drawLeafFX(leaves[i].oldX, leaves[i].oldY, leaves[i].size, bg);

    leaves[i].x += leaves[i].vx;
    leaves[i].phase += 0.03f;
    leaves[i].y = leaves[i].baseY + sinf(leaves[i].phase) * leaves[i].amp;

    if (leaves[i].x > 500) initLeafFX(i);

    if (leaves[i].y >= 300)
      drawLeafFX((int)leaves[i].x, (int)leaves[i].y, leaves[i].size, leaves[i].color);

    leaves[i].oldX = (int)leaves[i].x;
    leaves[i].oldY = (int)leaves[i].y;
  }
}


// ============================================================================
// PAGE AIR QUALITY
// ============================================================================
static void pageAir()
{
  const bool it = (g_lang == "it");

  drawHeader(it ? "Aria a " + sanitizeText(g_city)
                : "Air quality – " + sanitizeText(g_city));

  int cat = -1;
  String verdict = airVerdict(cat);

  uint16_t bg = COL_BG;
  switch (cat) {
    case 0: bg = AIR_BG_GOOD; break;
    case 1: bg = AIR_BG_FAIR; break;
    case 2: bg = AIR_BG_MODERATE; break;
    case 3: bg = AIR_BG_POOR; break;
    case 4: bg = AIR_BG_VERYPOOR; break;
  }
  g_air_bg = bg;

  gfx->fillRect(0, PAGE_Y, 480, 480 - PAGE_Y, bg);

  int y = PAGE_Y + 14;

  auto row = [&](const char* lbl, float v)
  {
    const uint8_t SZ = 3;

    gfx->setTextSize(SZ);
    gfx->setTextColor(COL_TEXT, bg);

    gfx->setCursor(PAGE_X, y);
    gfx->print(lbl);

    char buf[16];
    if (isnan(v)) strcpy(buf, "--");
    else dtostrf(v, 0, (lbl[2]=='2' && lbl[3]=='5') ? 1 : 0, buf);

    const char* unit = it ? " ug/m3" : " µg/m³";
    char out[24];
    snprintf(out, sizeof(out), "%s%s", buf, unit);

    int textW = strlen(out) * BASE_CHAR_W * SZ;
    int xr = 480 - PAGE_X - textW;
    if (xr < PAGE_X + 120) xr = PAGE_X + 120;

    gfx->setCursor(xr, y);
    gfx->print(out);

    y += BASE_CHAR_H * SZ + 10;
  };

  row("PM2.5:", aq_pm25);
  row("PM10 :", aq_pm10);
  row(it ? "OZONO:" : "OZONE", aq_o3);
  row("NO2  :", aq_no2);

  drawHLine(y);
  y += 14;

  gfx->setTextSize(TEXT_SCALE + 1);
  gfx->setTextColor(COL_TEXT, bg);

  int maxW = 480 - PAGE_X * 2;
  int maxC = maxW / (BASE_CHAR_W * (TEXT_SCALE + 1));
  if (maxC < 10) maxC = 10;

  String s = sanitizeText(verdict);

  int start = 0;
  y += 12;

  while (start < s.length()) {
    int len = min(maxC, (int)s.length() - start);
    int cut = start + len;

    if (cut < s.length()) {
      int sp = s.lastIndexOf(' ', cut);
      if (sp > start) cut = sp;
    }

    String line = s.substring(start, cut);
    line.trim();

    int w = line.length() * BASE_CHAR_W * (TEXT_SCALE + 1);
    int x = (480 - w) / 2;
    if (x < PAGE_X) x = PAGE_X;

    gfx->setCursor(x, y);
    gfx->print(line);

    y += BASE_CHAR_H * (TEXT_SCALE + 1) + 6;
    start = (cut < s.length() && s.charAt(cut)==' ') ? cut + 1 : cut;

    if (y > 420) break;
  }

  gfx->setTextSize(TEXT_SCALE);

  tickLeaves(bg);
}

