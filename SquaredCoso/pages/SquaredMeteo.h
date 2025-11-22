#pragma once
/*
===============================================================================
   SQUARED — PAGINA METEO (wttr.in JSON) +  PARALLASSE 3 LIVELLI

   Effetti particelle:
   --------------------
   - 3 livelli: vicino / medio / lontano
   - Ogni livello ha:
       • velocità diversa
       • ampiezza oscillazione diversa
       • luminosità diversa
   - Movimento:
       • drift verticale continuo
       • oscillazione sinusoidale
       • flutter leggero orizzontale
   - Overlay tipo sprite:
       • cancellazione SOLO pixel precedenti
       • MAI cancellare testo, icone, linee
===============================================================================
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "../handlers/globals.h"

// ============================================================================
// EXTERN
// ============================================================================
extern const uint16_t COL_BG;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;

extern Arduino_RGB_Display* gfx;

extern const int PAGE_X;
extern const int PAGE_Y;

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern void drawHLine(int y);
extern bool httpGET(const String& url, String& body, uint32_t timeoutMs);
extern String sanitizeText(const String& in);
extern int indexOfCI(const String& src, const String& key, int from);

extern String g_city;
extern String g_lang;

// ============================================================================
// ICONe
// ============================================================================
#include "../images/pioggia.h"
#include "../images/sole.h"
#include "../images/nuvole.h"

// ============================================================================
// ESTRAZIONE DATI METEO
// ============================================================================
static float  w_now_tempC = NAN;
static String w_now_desc  = "";
static String w_desc[3]   = { "", "", "" };

// ============================================================================
// PARTICELLE — MOTO CIRCOLARE (NO DRIFT VERTICALE/ORIZZONTALE INDIPENDENTE)
// ============================================================================

#define N_DUST 40   // più particelle

struct Particle {
  float baseX, baseY;   // centro del cerchio
  float angle;          // angolo corrente
  float speed;          // velocità angolare
  float radius;         // raggio del cerchio

  int16_t x, y;         // posizione attuale
  int16_t oldX, oldY;   // posizione precedente
  uint8_t bright;       // livelli luminosità
  uint8_t layer;        // 0=far, 1=mid, 2=near
};

static Particle dust[N_DUST];
static bool dustInit = false;
static uint32_t dustLastMs = 0;


// ============================================================================
// CONTROLLA SE UN PIXEL È UI (NON TOCCARE MAI)
// Qui mascheriamo:
//
// - Header in alto
// - Linea "temp + descrizione" subito sotto
// - Colonna sinistra con giorni / descrizioni
// - Temperatura  in basso a sinistra
// - Icona meteo in basso a destra
// ============================================================================
static inline bool isUIpixel(int x, int y)
{
  // 1) HEADER + linea principale meteo
  //    Tutto quello sopra ~PAGE_Y+80 lo consideriamo UI sicura
  if (y < PAGE_Y + 80) return true;

  // 2) COLONNA TESTO SINISTRA (giorni + descrizioni)
  //    Larghezza ~320 px, altezza fino a ~320 px
  if (x >= PAGE_X && x < 320 && y >= PAGE_Y && y < 320)
    return true;

  // 3) TEMPERATURA GRANDE IN BASSO A SINISTRA
  //    Nell'originale: setCursor(PAD, 480 - 90) con PAD=24
  if (x >= 0 && x < 260 && y > 360)
    return true;

  // 4) ICONA METEO IN BASSO A DESTRA
  //    La posizione finale è: ix = 480 - PIOGGIA_WIDTH - PAD;
  //                           iy = 480 - PIOGGIA_HEIGHT - PAD;
  const int PAD = 24;
  const int iconMaxW = PIOGGIA_WIDTH;   // sono tutti simili, uso PIOGGIA come bounds
  const int iconMaxH = PIOGGIA_HEIGHT;

  int iconLeft  = 480 - iconMaxW - PAD - 4;  // un piccolo margine extra
  int iconTop   = 480 - iconMaxH - PAD - 4;

  if (x >= iconLeft && y >= iconTop)
    return true;

  // 5) LINEE ORIZZONTALI PRECISISSIME (di sicurezza)
  int y1 = PAGE_Y + 60;
  int y2 = PAGE_Y + 76;
  int y3 = PAGE_Y + 131;
  int y4 = PAGE_Y + 200;

  if (y == y1 || y == y2 || y == y3 || y == y4)
    return true;

  return false;
}



// ============================================================================
// INIT PARTICELLA — definisce un cerchio e un movimento circolare random
// ============================================================================
static void initDustParticle(int i)
{
  uint8_t L = dust[i].layer;

  // scegli un centro di orbita che NON sia nella UI
  int tries = 0;
  do {
    dust[i].baseX = random(40, 440);
    dust[i].baseY = random(PAGE_Y + 90, 440);  // parto già sotto l'header
    tries++;
    if (tries > 10) break;  // evita loop infiniti
  } while (isUIpixel((int)dust[i].baseX, (int)dust[i].baseY));

  // raggio del cerchio (più grande → moto circolare visibile)
  if (L == 0)       dust[i].radius = 10 + random(0, 10);  // far
  else if (L == 1)  dust[i].radius = 18 + random(0, 14);  // mid
  else              dust[i].radius = 26 + random(0, 18);  // near

  // angolo iniziale random
  dust[i].angle = random(0, 628) / 100.0f;

  // velocità angolare (più alta → giro ben percepibile)
  if (L == 0)       dust[i].speed = 0.010f + random(0, 15) / 10000.0f;
  else if (L == 1)  dust[i].speed = 0.018f + random(0, 18) / 10000.0f;
  else              dust[i].speed = 0.026f + random(0, 24) / 10000.0f;

  // luminosità per layer
  dust[i].bright = (L == 0 ? 0 : (L == 1 ? 1 : 2));

  // calcola posizioni iniziali
  dust[i].x = dust[i].baseX + cosf(dust[i].angle) * dust[i].radius;
  dust[i].y = dust[i].baseY + sinf(dust[i].angle) * dust[i].radius;
  dust[i].oldX = dust[i].x;
  dust[i].oldY = dust[i].y;
}



// ============================================================================
// PARTICLE TICK — moto circolare, NO VERTICALE, NO DRIFT
// ============================================================================
void pageWeatherParticlesTick()
{
  uint32_t now = millis();
  if (now - dustLastMs < 33) return;  // ~30 FPS
  dustLastMs = now;

  if (!dustInit) {
    dustInit = true;

    // assegna layer
    for (int i = 0; i < N_DUST; i++) {
      if (i < 20)      dust[i].layer = 0;  // far
      else if (i < 45) dust[i].layer = 1;  // mid
      else             dust[i].layer = 2;  // near

      initDustParticle(i);
    }
  }

  static const uint16_t col[3] = {
    0xC618,
    0xE71C,
    0xFFFF
  };

  for (int i = 0; i < N_DUST; i++) {

    // 1) CANCELLA PIXEL PRECEDENTE SOLO SE NON UI
    if (!isUIpixel(dust[i].oldX, dust[i].oldY)) {
      gfx->drawPixel(dust[i].oldX, dust[i].oldY, COL_BG);
    }

    // 2) AGGIORNA ANGOLO → MOTO CIRCOLARE
    dust[i].angle += dust[i].speed;
    if (dust[i].angle > 6.28318f)
      dust[i].angle -= 6.28318f;

    // nuova posizione sul cerchio
    float nx = dust[i].baseX + cosf(dust[i].angle) * dust[i].radius;
    float ny = dust[i].baseY + sinf(dust[i].angle) * dust[i].radius;

    dust[i].x = (int16_t)nx;
    dust[i].y = (int16_t)ny;

    // se per qualche motivo orbita entra troppo in una zona piena di UI,
    // rigenera completamente la particella
    if (isUIpixel(dust[i].x, dust[i].y)) {
      initDustParticle(i);
      continue;
    }

    // 3) DISEGNA SOLO SE NON È UI
    uint16_t c = col[dust[i].bright];
    gfx->drawPixel(dust[i].x, dust[i].y, c);

    // 4) SALVA POSIZIONE
    dust[i].oldX = dust[i].x;
    dust[i].oldY = dust[i].y;
  }
}



// ============================================================================
// JSON SIMPLE
// ============================================================================
static bool jsonFindStringKV(const String& body,
                             const String& key,
                             int from,
                             String& outVal)
{
  int k = body.indexOf("\"" + key + "\"", from);
  if (k < 0) return false;

  int c = body.indexOf(':', k);
  int q1 = body.indexOf('"', c + 1);
  int q2 = body.indexOf('"', q1 + 1);

  if (c < 0 || q1 < 0 || q2 < 0) return false;

  outVal = body.substring(q1 + 1, q2);
  return true;
}

// ============================================================================
// TRADUZIONE
// ============================================================================
static String translateWeather(const String& s)
{
  if (g_lang != "it") return s;
  String t = s;
  t.toLowerCase();

  if (t.indexOf("sunny") >= 0) return "soleggiato";
  if (t.indexOf("clear") >= 0) return "sereno";
  if (t.indexOf("cloud") >= 0) return "nuvoloso";
  if (t.indexOf("rain") >= 0) return "pioggia";
  if (t.indexOf("snow") >= 0) return "neve";
  if (t.indexOf("storm") >= 0) return "temporale";
  if (t.indexOf("fog") >= 0) return "nebbia";

  return s;
}

// ============================================================================
// GEO: ottiene lat/lon da Open-Meteo se mancano
// ============================================================================
bool geocodeIfNeeded() {

  if (g_lat.length() && g_lon.length())
    return true;

  String url =
    "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json"
    "&name=" + g_city +
    "&language=" + g_lang;

  String body;
  if (!httpGET(url, body, 10000))
    return false;

  int p = indexOfCI(body, "\"latitude\"", 0);
  if (p < 0) return false;
  int c = body.indexOf(':', p);
  int e = body.indexOf(',', c + 1);
  g_lat = sanitizeText(body.substring(c + 1, e));

  p = indexOfCI(body, "\"longitude\"", 0);
  if (p < 0) return false;
  c = body.indexOf(':', p);
  e = body.indexOf(',', c + 1);
  g_lon = sanitizeText(body.substring(c + 1, e));

  if (!g_lat.length() || !g_lon.length())
    return false;

  saveAppConfig();
  return true;
}


// ============================================================================
// FETCH
// ============================================================================
bool fetchWeather()
{
  String url = "https://wttr.in/" + g_city + "?format=j1&lang=" + g_lang;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  https.begin(client, url);
  https.addHeader("User-Agent", "curl");
  https.addHeader("Accept", "application/json");

  int code = https.GET();
  if (code != 200) {
    https.end();
    return false;
  }

  String body = https.getString();
  https.end();

  // Meteo attuale -------------------------------------------------------
  int cc = indexOfCI(body, "\"current_condition\"", 0);
  if (cc >= 0) {
    String t;
    if (jsonFindStringKV(body, "temp_C", cc, t))
      w_now_tempC = t.toFloat();

    if (jsonFindStringKV(body, "value",
                         indexOfCI(body, "\"weatherDesc\"", cc), 
                         w_now_desc))
      w_now_desc = translateWeather(sanitizeText(w_now_desc));
  }

  // Previsioni ----------------------------------------------------------
  for (int i = 0; i < 3; i++) {
    int pos = indexOfCI(body, "\"weatherDesc\"", cc + i * 40);
    if (pos > 0) {
      String v;
      if (jsonFindStringKV(body, "value", pos, v))
        w_desc[i] = translateWeather(sanitizeText(v));
    }
  }

  return true;
}

// ============================================================================
// ICONA
// ============================================================================
static int pickWeatherIcon(const String& d)
{
  String s = d; s.toLowerCase();
  if (s.indexOf("sun") >= 0 || s.indexOf("sole") >= 0) return 0;
  if (s.indexOf("rain") >= 0 || s.indexOf("piogg") >= 0) return 2;
  return 1;
}

// ============================================================================
// PAGE WEATHER
// ============================================================================
void pageWeather()
{
  drawHeader(g_lang == "it"
               ? "Meteo per " + sanitizeText(g_city)
               : "Weather in " + sanitizeText(g_city));

  int y = PAGE_Y;

  String line =
    (!isnan(w_now_tempC) && w_now_desc.length())
    ? String((int)round(w_now_tempC)) + "c  " + w_now_desc
    : (g_lang == "it" ? "Sto aggiornando..." : "Updating...");

  drawBoldMain(PAGE_X, y + 20, line, 2);
  y += 60;

  drawHLine(y);
  y += 16;

  static const char* it_days[3] = { "oggi", "domani", "fra 2 giorni" };
  static const char* en_days[3] = { "today", "tomorrow", "in 2 days" };

  for (int i = 0; i < 3; i++) {
    gfx->setCursor(PAGE_X, y); gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->print(g_lang == "it" ? it_days[i] : en_days[i]);

    gfx->setCursor(PAGE_X, y + 20); gfx->setTextColor(COL_ACCENT2, COL_BG);
    gfx->print(w_desc[i]);

    y += 55;
    if (i < 2) {
      drawHLine(y);
      y += 14;
    }
  }

  int type = pickWeatherIcon(w_now_desc);
  int PAD = 24;

  int ix = 480 - PIOGGIA_WIDTH - PAD;
  int iy = 480 - PIOGGIA_HEIGHT - PAD;

  if (type == 0)
    gfx->draw16bitRGBBitmap(ix, iy, (uint16_t*)sole_img, SOLE_WIDTH, SOLE_HEIGHT);
  else if (type == 1)
    gfx->draw16bitRGBBitmap(ix, iy, (uint16_t*)nuvole_img, NUVOLE_WIDTH, NUVOLE_HEIGHT);
  else
    gfx->draw16bitRGBBitmap(ix, iy, (uint16_t*)pioggia_img, PIOGGIA_WIDTH, PIOGGIA_HEIGHT);

  // temperatura grande
  if (!isnan(w_now_tempC)) {
    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->setTextSize(9);
    gfx->setCursor(PAD, 480 - 90);
    gfx->print(String((int)round(w_now_tempC)) + "c");
    gfx->setTextSize(2);
  }
}

