#pragma once
/*
===============================================================================
   SQUARED — PAGINA “AIR QUALITY” + FOGLIE PIXEL-ART (overlay inferiore)
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
// CACHE
// ============================================================================
static float aq_pm25 = NAN;
static float aq_pm10 = NAN;
static float aq_o3   = NAN;
static float aq_no2  = NAN;


// ============================================================================
// JSON UTILS
// ============================================================================
static bool extractObjectBlock(const String& body,
                               const String& key,
                               String& out)
{
  int k = indexOfCI(body, String("\"") + key + "\"", 0);
  if (k < 0) return false;

  int b = body.indexOf('{', k);
  if (b < 0) return false;

  int depth = 0;
  for (int i = b; i < body.length(); i++) {
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
  String k = String("\"") + key + "\"";
  int p = indexOfCI(obj, k, 0);
  if (p < 0) return NAN;

  int a = obj.indexOf('[', p);
  if (a < 0) return NAN;

  int s = a + 1;
  while (s < obj.length() && isspace(obj[s])) s++;

  if (obj[s] == '"') return NAN;

  int e = s;
  while (e < obj.length() && obj[e] != ',' && obj[e] != ']') e++;

  String num = obj.substring(s, e);
  num.trim();
  return num.toFloat();
}


// ============================================================================
// CLASSIFICAZIONE
// ============================================================================
static int catFrom(float v, float a, float b, float c, float d) {
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

  int c = catFrom(aq_pm25, 10, 20, 25, 50);   if (c > worst) worst = c;
  c = catFrom(aq_pm10, 20, 40, 50, 100);      if (c > worst) worst = c;
  c = catFrom(aq_o3,   80, 120, 180, 240);    if (c > worst) worst = c;
  c = catFrom(aq_no2,  40, 90, 120, 230);     if (c > worst) worst = c;

  categoryOut = worst;

  if (worst < 0)
    return (g_lang == "it")
            ? "Aria: dati non disponibili"
            : "Air: data unavailable";

  static const char* cat_it[] = {
    "Aria buona","Aria discreta","Aria moderata","Aria scadente","Aria pessima"
  };
  static const char* cat_en[] = {
    "Good air","Fair air","Moderate air","Poor air","Very poor air"
  };
  static const char* tip_it[] = {
    "tutto ok!","nessuna precauzione.","sensibili: limitare sforzi intensi.",
    "evitare attività all'aperto prolungate!","rimanere al chiuso!"
  };
  static const char* tip_en[] = {
    "all good!","no precautions needed.","sensitive groups: reduce heavy effort.",
    "avoid prolonged outdoor activity!","stay indoors!"
  };

  return (g_lang == "it")
           ? String(cat_it[worst]) + ", " + tip_it[worst]
           : String(cat_en[worst]) + ", " + tip_en[worst];
}


// ============================================================================
// FETCH
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
  if (!extractObjectBlock(body, "hourly", hourlyBlk))
    return false;

  aq_pm25 = parseFirstNumber(hourlyBlk, "pm2_5");
  aq_pm10 = parseFirstNumber(hourlyBlk, "pm10");
  aq_o3   = parseFirstNumber(hourlyBlk, "ozone");
  aq_no2  = parseFirstNumber(hourlyBlk, "nitrogen_dioxide");

  return true;
}


// ============================================================================
// FOGLIE PIXEL-ART (overlay inferiore)
// ============================================================================
// ============================================================================
// FOGLIE PIXEL-ART — SISTEMA IDENTICO AL PARTICELLARE METEO
// ============================================================================
#define N_LEAVES 50

struct Leaf {
  float x, y;
  float baseY;
  float phase;
  float speed;
  float vx;
  float amp;
  int16_t oldX, oldY;
  uint8_t size;
  uint16_t color;
};

static Leaf leaves[N_LEAVES];
static bool leavesInit = false;
static uint32_t leavesLastMs = 0;

static const uint16_t LEAF_COLORS[2] = {
  0xFD80,   // arancione
  0xA440    // marrone chiaro
};

// genera foglia
static void initLeafFX(int i)
{
  leaves[i].x = random(-50, -10);
  leaves[i].y = random(310, 470);
  leaves[i].baseY = leaves[i].y;

  leaves[i].vx = 1.0f + random(0, 20) / 10.0f;
  leaves[i].amp = 6 + random(0, 10);
  leaves[i].phase = random(0, 628) / 100.0f;
  leaves[i].speed = 0.02f + random(0, 10) / 500.0f;

  leaves[i].size = random(0, 3);
  leaves[i].color = LEAF_COLORS[random(0, 2)];

  leaves[i].oldX = -1;
  leaves[i].oldY = -1;
}

static void drawLeafFX(int x, int y, uint8_t s, uint16_t c)
{
  if (s == 0) {
    gfx->drawPixel(x, y, c);
    gfx->drawPixel(x-1, y, c);
    gfx->drawPixel(x+1, y, c);
  }
  else if (s == 1) {
    gfx->drawPixel(x, y, c);
    gfx->drawPixel(x-1, y, c);
    gfx->drawPixel(x+1, y, c);
    gfx->drawPixel(x, y-1, c);
    gfx->drawPixel(x, y+1, c);
  }
  else {
    gfx->drawPixel(x, y, c);
    gfx->drawPixel(x-1, y, c);
    gfx->drawPixel(x+1, y, c);
    gfx->drawPixel(x, y-1, c);
    gfx->drawPixel(x, y+1, c);
    gfx->drawPixel(x-1, y-1, c);
    gfx->drawPixel(x+1, y-1, c);
    gfx->drawPixel(x-1, y+1, c);
    gfx->drawPixel(x+1, y+1, c);
  }
}


// ============================================================================
// tickLeaves — IDENTICO AL PARTICELLARE METEO
// ============================================================================
void tickLeaves(uint16_t bg)
{
  uint32_t now = millis();
  if (now - leavesLastMs < 33) return;  // ~30 FPS
  leavesLastMs = now;

  if (!leavesInit) {
    leavesInit = true;
    for (int i = 0; i < N_LEAVES; i++)
      initLeafFX(i);
  }

  for (int i = 0; i < N_LEAVES; i++) {

    // cancella posizione precedente
    if (leaves[i].oldY >= 300) {
      drawLeafFX(leaves[i].oldX, leaves[i].oldY, leaves[i].size, bg);
    }

    // aggiorna movimento
    leaves[i].x += leaves[i].vx;
    leaves[i].phase += leaves[i].speed;
    leaves[i].y = leaves[i].baseY + sinf(leaves[i].phase) * leaves[i].amp;

    // ricomparsa
    if (leaves[i].x > 500)
      initLeafFX(i);

    // disegno nuovo
    if (leaves[i].y >= 300)
      drawLeafFX((int)leaves[i].x, (int)leaves[i].y, leaves[i].size, leaves[i].color);

    // salva posizioni
    leaves[i].oldX = (int)leaves[i].x;
    leaves[i].oldY = (int)leaves[i].y;
  }
}



// ============================================================================
// pageAir()
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
    case 0: bg = AIR_BG_GOOD;      break;
    case 1: bg = AIR_BG_FAIR;      break;
    case 2: bg = AIR_BG_MODERATE;  break;
    case 3: bg = AIR_BG_POOR;      break;
    case 4: bg = AIR_BG_VERYPOOR;  break;
  }
  g_air_bg = bg;

  // sfondo pagina
  gfx->fillRect(0, PAGE_Y, 480, 480 - PAGE_Y, bg);

  int y = PAGE_Y + 14;

  // righe valori
  auto row = [&](const char* lbl, float v)
  {
    const uint8_t SZ = 3;
    gfx->setTextSize(SZ);
    gfx->setTextColor(COL_TEXT, bg);

    gfx->setCursor(PAGE_X, y);
    gfx->print(lbl);

    const char* unit = it ? " ug/m3" : " µg/m³";
    String val = isnan(v) ? "--" : String(v, (lbl[2]=='2' && lbl[3]=='5') ? 1 : 0);
    val += unit;

    int textW = val.length() * BASE_CHAR_W * SZ;
    int xRight = 480 - PAGE_X - textW;
    if (xRight < PAGE_X + 120) xRight = PAGE_X + 120;

    gfx->setCursor(xRight, y);
    gfx->print(val);

    y += BASE_CHAR_H * SZ + 10;
  };

  const char* l_pm25 = "PM2.5 :";
  const char* l_pm10 = "PM10  :";
  const char* l_o3   = it ? "OZONO :" : "OZONE:";
  const char* l_no2  = "NO2   :";

  row(l_pm25, aq_pm25);
  row(l_pm10, aq_pm10);
  row(l_o3,   aq_o3);
  row(l_no2,  aq_no2);

  drawHLine(y);
  y += 14;

  // testo descrittivo
  uint8_t vScale = TEXT_SCALE + 1;
  gfx->setTextSize(vScale);
  gfx->setTextColor(COL_TEXT, bg);

  int maxWidth = 480 - PAGE_X * 2;
  int maxChars = maxWidth / (BASE_CHAR_W * vScale);
  if (maxChars < 10) maxChars = 10;

  String s = sanitizeText(verdict);
  int start = 0;
  y += 12;

  while (start < s.length()) {
    int len = min(maxChars, (int)s.length() - start);
    int cut = start + len;

    if (cut < s.length()) {
      int sp = s.lastIndexOf(' ', cut);
      if (sp > start) cut = sp;
    }

    String line = s.substring(start, cut);
    line.trim();

    int lineW = line.length() * BASE_CHAR_W * vScale;
    int x = (480 - lineW) / 2;
    if (x < PAGE_X) x = PAGE_X;

    gfx->setCursor(x, y);
    gfx->print(line);

    y += BASE_CHAR_H * vScale + 6;
    start = (cut < s.length() && s.charAt(cut)==' ') ? cut + 1 : cut;

    if (y > 420) break;
  }

  gfx->setTextSize(TEXT_SCALE);

  // === ANIMAZIONE FOGLIE (ESEGUITA DOPO LA UI) ===
  tickLeaves(bg);
}

