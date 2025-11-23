#pragma once
/*
===============================================================================
   SQUARED — PAGINA SUN (Alba / Tramonto / Durata luce)
   Versione ultra-ottimizzata — ESP32 safe — FX visibile sempre
===============================================================================
*/

#include <Arduino.h>
#include <time.h>
#include "../handlers/globals.h"

// ---------------------------------------------------------------------------
// EXTERN
// ---------------------------------------------------------------------------
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

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern bool httpGET(const String& url, String& body, uint32_t timeoutMs);
extern String sanitizeText(const String& in);

extern String g_lat, g_lon, g_lang;



// ============================================================================
// CACHE (solo char[], zero String temporanee)
// ============================================================================
static char sun_rise[6];
static char sun_set [6];
static char sun_noon[6];
static char sun_cb  [6];
static char sun_ce  [6];
static char sun_len [16];


// ============================================================================
// timegm() compatibile ESP32 (conversione UTC → epoch manuale)
// ============================================================================
static time_t my_timegm(const struct tm *t)
{
    static const int mdays[12] =
      {31,28,31,30,31,30,31,31,30,31,30,31};

    int year  = t->tm_year + 1900;
    int month = t->tm_mon;
    int day   = t->tm_mday;

    long days = 0;

    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))
            days++;
    }

    for (int m = 0; m < month; m++) {
        days += mdays[m];
        if (m == 1) {
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
                days++;
        }
    }

    days += (day - 1);

    return (time_t)(
         days       * 86400L +
         t->tm_hour * 3600L +
         t->tm_min  * 60L +
         t->tm_sec
    );
}


// ============================================================================
// ISO → HH:MM
// ============================================================================
static inline void isoToHM(const String& iso, char out[6])
{
    int t = iso.indexOf('T');
    if (t < 0 || t + 5 >= iso.length()) {
        strcpy(out, "--:--");
        return;
    }
    out[0] = iso[t+1];
    out[1] = iso[t+2];
    out[2] = ':';
    out[3] = iso[t+4];
    out[4] = iso[t+5];
    out[5] = 0;
}


// ============================================================================
// ISO8601 → epoch UTC
// ============================================================================
static bool isoToEpoch(const String& s, time_t& out)
{
    if (s.length() < 19) return false;

    struct tm tt {};
    tt.tm_year = s.substring(0,4).toInt() - 1900;
    tt.tm_mon  = s.substring(5,7).toInt() - 1;
    tt.tm_mday = s.substring(8,10).toInt();
    tt.tm_hour = s.substring(11,13).toInt();
    tt.tm_min  = s.substring(14,16).toInt();
    tt.tm_sec  = s.substring(17,19).toInt();

    out = my_timegm(&tt);
    return true;
}


// ============================================================================
// JSON estrazione semplice "key":"value"
// ============================================================================
static bool jsonKV(const String& body, const char* key, String& out)
{
    String k = "\"" + String(key) + "\"";
    int p = body.indexOf(k);
    if (p < 0) return false;

    p = body.indexOf('"', p + k.length());
    if (p < 0) return false;

    int q = body.indexOf('"', p + 1);
    if (q < 0) return false;

    out = body.substring(p + 1, q);
    return true;
}


// ============================================================================
// FETCH DATI
// ============================================================================
bool fetchSun()
{
    strcpy(sun_rise, "--:--");
    strcpy(sun_set,  "--:--");
    strcpy(sun_noon, "--:--");
    strcpy(sun_len,  "--h --m");
    strcpy(sun_cb,   "--:--");
    strcpy(sun_ce,   "--:--");

    if (g_lat.isEmpty() || g_lon.isEmpty())
        return false;

    String url =
      "https://api.sunrise-sunset.org/json?lat=" + g_lat +
      "&lng=" + g_lon + "&formatted=0";

    String body;
    if (!httpGET(url, body, 10000))
        return false;

    String sr, ss, sn, cb, ce;

    if (!jsonKV(body, "sunrise", sr)) return false;
    if (!jsonKV(body, "sunset",  ss)) return false;

    jsonKV(body, "solar_noon",           sn);
    jsonKV(body, "civil_twilight_begin", cb);
    jsonKV(body, "civil_twilight_end",   ce);

    isoToHM(sr, sun_rise);
    isoToHM(ss, sun_set);
    isoToHM(sn, sun_noon);
    isoToHM(cb, sun_cb);
    isoToHM(ce, sun_ce);

    // durata luce
    time_t a, b;
    if (isoToEpoch(sr, a) && isoToEpoch(ss, b) && b > a) {
        long d = b - a;
        snprintf(sun_len, sizeof(sun_len),
                 "%02ldh %02ldm",
                 d / 3600, (d % 3600) / 60);
    }

    return true;
}


// ============================================================================
// UI — RIGA
// ============================================================================
static inline void sunRow(const char* label, const char* val, int& y)
{
    const int SZ = TEXT_SCALE + 1;
    const int H  = BASE_CHAR_H * SZ + 8;

    gfx->setTextSize(SZ);

    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->setCursor(PAGE_X, y);
    gfx->print(label);

    gfx->setTextColor(COL_ACCENT2, COL_BG);
    int w = strlen(val) * BASE_CHAR_W * SZ;
    gfx->setCursor(480 - PAGE_X - w, y);
    gfx->print(val);

    y += H;
}


// ============================================================================
// FX — ESPLOSIONI VISIBILI, LUMINOSE, SICURE
// ============================================================================
#define SUN_FX_MAX     18
#define SUN_FX_PARTS    8

static const int8_t SUN_DX[SUN_FX_PARTS] PROGMEM = {1,-1,2,-2,1,-1,2,-2};
static const int8_t SUN_DY[SUN_FX_PARTS] PROGMEM = {2,2,1,1,-2,-2,-1,-1};

struct SunBurst {
    bool active;
    int x, y;
    int px[SUN_FX_PARTS];
    int py[SUN_FX_PARTS];
    uint8_t frame;
    uint8_t maxF;
};

static SunBurst sb[SUN_FX_MAX];
static uint32_t sbLast = 0;

static int SUN_FX_YMIN = 320;
static int SUN_FX_YMAX = 479;


// avvia una nuova esplosione
static inline void sunFxStart()
{
    for (int i = 0; i < SUN_FX_MAX; i++) {
        if (!sb[i].active) {
            SunBurst& b = sb[i];

            b.active = true;
            b.frame  = 0;
            b.maxF   = 10 + random(6);

            b.x = random(60, 420);
            b.y = random(SUN_FX_YMIN + 10, SUN_FX_YMAX - 10);

            for (int p = 0; p < SUN_FX_PARTS; p++) {
                b.px[p] = b.x;
                b.py[p] = b.y;
            }
            return;
        }
    }
}


// tick animazione
static void sunFxTick()
{
    uint32_t now = millis();

    if (now - sbLast >= 120) {
        sbLast = now;
        sunFxStart();
    }

    for (int i = 0; i < SUN_FX_MAX; i++) {

        SunBurst& b = sb[i];
        if (!b.active) continue;

        uint8_t f = b.frame;
        uint16_t fade = 255 - (f * (255 / b.maxF));

        // colore luminoso, tipo flare giallo-arancio
        uint16_t col =
            ((fade >> 1) << 11) |   // R forte
            (fade << 5)        |   // G pieno
            (fade >> 2);           // B medio

        for (int p = 0; p < SUN_FX_PARTS; p++) {

            // cancella
            gfx->drawPixel(b.px[p], b.py[p], COL_BG);

            // sposta
            b.px[p] += pgm_read_byte(&SUN_DX[p]);
            b.py[p] += pgm_read_byte(&SUN_DY[p]);

            if (b.py[p] >= SUN_FX_YMIN &&
                b.py[p] <  SUN_FX_YMAX &&
                b.px[p] >= 0 &&
                b.px[p] <  480)
            {
                gfx->drawPixel(b.px[p], b.py[p], col);
            }
        }

        b.frame++;
        if (b.frame >= b.maxF)
            b.active = false;
    }
}

// ============================================================================
// WRAPPER RICHIAMABILE DAL MAIN
// ============================================================================
void tickSunFX()
{
    sunFxTick();
}


// ============================================================================
// RENDER
// ============================================================================
void pageSun()
{
    const bool it = (g_lang == "it");

    drawHeader(it ? "Ore di luce oggi"
                  : "Today's daylight");

    int y = PAGE_Y + 20;

    if (sun_rise[0] == '-' || sun_set[0] == '-') {
        drawBoldMain(PAGE_X, y,
                     it ? "Nessun dato disponibile"
                        : "No data available",
                     TEXT_SCALE);
        return;
    }

    sunRow(it ? "Alba"          : "Sunrise",     sun_rise, y);
    sunRow(it ? "Tramonto"      : "Sunset",      sun_set,  y);
    sunRow(it ? "Mezzogiorno"   : "Solar noon",  sun_noon, y);
    sunRow(it ? "Durata luce"   : "Day length",  sun_len,  y);
    sunRow(it ? "Civile inizio" : "Civil begin", sun_cb,   y);
    sunRow(it ? "Civile fine"   : "Civil end",   sun_ce,   y);

    // aggiorna area FX dinamicamente
    SUN_FX_YMIN = y + 10;
    SUN_FX_YMAX = 479;

    // animazione
    sunFxTick();
}

