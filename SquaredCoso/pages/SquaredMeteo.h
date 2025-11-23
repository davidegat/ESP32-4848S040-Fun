#pragma once

/*
===============================================================================
 SQUARED — METEO + PARTICELLE FULLSCREEN
 Particelle lente ovunque, UI protetta SOLO dove necessario
===============================================================================
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../handlers/globals.h"

// === EXTERN ===
extern Arduino_RGB_Display* gfx;
extern const uint16_t COL_BG;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;

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

// === ICONS ===
#include "../images/pioggia.h"
#include "../images/sole.h"
#include "../images/nuvole.h"

// === DATI METEO ===
static float  w_now_tempC = NAN;
static String w_now_desc  = "";
static String w_desc[3]   = { "", "", "" };


// ============================================================================
// UI MASK — versione RIDOTTA per far passare le particelle ovunque
// ============================================================================
static inline bool isUIpixel(int x, int y)
{
    // HEADER (sempre davanti)
    if (y < PAGE_Y + 60)
        return true;

    // LINEA 1 (dopo temperatura grande)
    if (y == PAGE_Y + 60)
        return true;

    // BLOCCO TESTO 3 GIORNI (solo testo, non le linee)
    if (y >= PAGE_Y + 100 && y <= PAGE_Y + 120)
        return true;

    if (y >= PAGE_Y + 120 && y <= PAGE_Y + 240)
        return true;

    // LINEA 2 (tra giorno 1 e 2)
    if (y == PAGE_Y + 175)
        return true;

    // LINEA 3 (tra giorno 2 e 3)
    if (y == PAGE_Y + 244)
        return true;

    // TEMPERATURA GRANDE in basso a sinistra (non deve essere toccata)
    if (x < 200 && y >= 360)
        return true;

    // ICONA meteo in basso a destra
    const int PAD = 24;
    int left  = 480 - PIOGGIA_WIDTH  - PAD - 4;
    int top   = 480 - PIOGGIA_HEIGHT - PAD - 4;
    if (x >= left && y >= top)
        return true;

    return false;
}




// ============================================================================
// PARTICELLE — FULLSCREEN VERAMENTE
// ============================================================================
#define N_DUST   80
#define ANG_RES  128
#define ANG_MASK (ANG_RES - 1)

struct Particle {
    int16_t cx, cy;
    int16_t x, y;
    int16_t ox, oy;
    uint8_t a, speed, r, layer;
};

static Particle dust[N_DUST];
static bool dustInit = false;
static uint32_t dustLast = 0;

// ============================================================================
// LUT SENO / COSENO x32 — 128 valori precomputati, nessun file esterno
// ============================================================================
static const int8_t LUT_X[ANG_RES] PROGMEM = {
    32,31,31,30,29,28,26,24,22,20,18,16,13,10, 7, 4,
     2, -1, -4, -7, -9,-12,-14,-17,-19,-22,-24,-26,-28,-29,-30,-31,
    -32,-31,-31,-30,-29,-28,-26,-24,-22,-20,-18,-16,-13,-10, -7, -4,
    -2,  1,  4,  7,  9, 12, 14, 17, 19, 22, 24, 26, 28, 29, 30, 31,
     32,31,31,30,29,28,26,24,22,20,18,16,13,10, 7, 4,
     2, -1, -4, -7, -9,-12,-14,-17,-19,-22,-24,-26,-28,-29,-30,-31,
    -32,-31,-31,-30,-29,-28,-26,-24,-22,-20,-18,-16,-13,-10, -7, -4,
    -2,  1,  4,  7,  9, 12, 14, 17, 19, 22, 24, 26, 28, 29, 30, 31
};

static const int8_t LUT_Y[ANG_RES] PROGMEM = {
     0, 2, 4, 7, 9,12,14,17,19,21,23,25,26,27,29,30,
    31, 31,31,30,29,28,26,24,22,20,18,16,13,10, 7, 4,
     2, 0,-2,-4,-7,-9,-12,-14,-17,-19,-21,-23,-25,-26,-27,-29,
    -30,-31,-31,-31,-30,-29,-28,-26,-24,-22,-20,-18,-16,-13,-10,-7,
    -4,-2, 0, 2, 4, 7, 9,12,14,17,19,21,23,25,26,27,
    29,30,31,31,31,30,29,28,26,24,22,20,18,16,13,10,
     7, 4, 2, 0,-2,-4,-7,-9,-12,-14,-17,-19,-21,-23,-25,-26,
    -27,-29,-30,-31,-31,-31,-30,-29,-28,-26,-24,-22,-20,-18,-16,-13
};


// — colori —
static const uint16_t dustCol[3] = {
    0x7BEF,   // far
    0xAD75,   // mid
    0xFFFF    // near
};

static inline void initDust(int i)
{
    Particle &p = dust[i];

    p.cx = random(5, 475);
    p.cy = random(5, 475);

    p.layer = dust[i].layer;   // già assegnato
    p.r = (p.layer == 0 ? 10+random(8) :
           p.layer == 1 ? 16+random(12) :
                           26+random(20));

    p.a = random(ANG_RES);
    p.speed = 1;  // lento

    int8_t lx = pgm_read_byte(&LUT_X[p.a]);
    int8_t ly = pgm_read_byte(&LUT_Y[p.a]);

    p.x  = p.cx + ((lx * p.r) >> 5);
    p.y  = p.cy + ((ly * p.r) >> 5);

    p.ox = p.x;
    p.oy = p.y;
}


static void pageWeatherParticlesTick()
{
    uint32_t now = millis();
    if (now - dustLast < 40) return;
    dustLast = now;

    if (!dustInit) {
        dustInit = true;
        for (int i = 0; i < N_DUST; i++) {
            dust[i].layer = (i < 14 ? 0 : (i < 28 ? 1 : 2));
            initDust(i);
        }
    }

    for (int i = 0; i < N_DUST; i++) {

        Particle &p = dust[i];

        // cancella pixel precedente se NON UI
        if (!isUIpixel(p.ox, p.oy))
            gfx->drawPixel(p.ox, p.oy, COL_BG);

        p.a = (p.a + p.speed) & ANG_MASK;

        int8_t lx = pgm_read_byte(&LUT_X[p.a]);
        int8_t ly = pgm_read_byte(&LUT_Y[p.a]);

        p.x = p.cx + ((lx * p.r) >> 5);
        p.y = p.cy + ((ly * p.r) >> 5);

        // reinizializza se entra nella UI
        if (p.x < 0 || p.x >= 480 ||
            p.y < 0 || p.y >= 480 ||
            isUIpixel(p.x, p.y))
        {
            initDust(i);
            continue;
        }

        gfx->drawPixel(p.x, p.y, dustCol[p.layer]);

        p.ox = p.x;
        p.oy = p.y;
    }
}


// ============================================================================
// JSON HELPERS
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
static String translateWeather(String s)
{
    if (g_lang != "it") return s;

    s.toLowerCase();
    if (s.indexOf("sun") >= 0) return "soleggiato";
    if (s.indexOf("clear")>=0) return "sereno";
    if (s.indexOf("cloud")>=0) return "nuvoloso";
    if (s.indexOf("rain")>=0) return "pioggia";
    if (s.indexOf("snow")>=0) return "neve";
    if (s.indexOf("storm")>=0) return "temporale";
    if (s.indexOf("fog")>=0) return "nebbia";

    return s;
}



// ============================================================================
// FETCH METEO
// ============================================================================
bool fetchWeather()
{
    String url =
        "https://wttr.in/" + g_city +
        "?format=j1&lang=" + g_lang;

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

    int cc = indexOfCI(body, "\"current_condition\"", 0);
    if (cc >= 0) {
        String t;
        if (jsonFindStringKV(body, "temp_C", cc, t))
            w_now_tempC = t.toFloat();

        if (jsonFindStringKV(body, "value",
                             indexOfCI(body, "\"weatherDesc\"", cc),
                             w_now_desc))
        {
            w_now_desc = translateWeather(sanitizeText(w_now_desc));
        }
    }

    for (int i = 0; i < 3; i++) {
        int pos = indexOfCI(body, "\"weatherDesc\"", cc + i * 60);
        if (pos > 0) {
            String v;
            if (jsonFindStringKV(body, "value", pos, v))
                w_desc[i] = translateWeather(sanitizeText(v));
        }
    }

    return true;
}



// ============================================================================
// ICON PICKER
// ============================================================================
static inline int pickWeatherIcon(const String& d)
{
    String s = d; s.toLowerCase();
    if (s.indexOf("sun")>=0 || s.indexOf("sole")>=0) return 0;
    if (s.indexOf("rain")>=0 || s.indexOf("piogg")>=0) return 2;
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
        gfx->setCursor(PAGE_X, y);
        gfx->setTextColor(COL_ACCENT1, COL_BG);
        gfx->print(g_lang == "it" ? it_days[i] : en_days[i]);

        gfx->setCursor(PAGE_X, y + 20);
        gfx->setTextColor(COL_ACCENT2, COL_BG);
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

    if (!isnan(w_now_tempC)) {
        gfx->setTextColor(COL_ACCENT1, COL_BG);
        gfx->setTextSize(9);
        gfx->setCursor(PAD, 480 - 90);
        gfx->print(String((int)round(w_now_tempC)) + "c");
        gfx->setTextSize(2);
    }
}

