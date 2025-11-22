#pragma once

/*
===============================================================================
   SQUARED — PAGINA “SUN” (Alba / Tramonto / Durata luce)

   - Fetch da sunrise-sunset.org (ISO8601 UTC)
   - Conversione ISO → HH:MM
   - Calcolo LOCALE della durata della luce (tramonto - alba)
   - Traduzione IT/EN
   - Layout stile ARIA, tutto automatico
===============================================================================
*/

#include <Arduino.h>
#include <time.h>
#include "../handlers/globals.h"

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
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern String sanitizeText(const String& in);

extern int indexOfCI(const String& src, const String& key, int from);
extern bool httpGET(const String& url, String& body, uint32_t timeoutMs);

extern String g_lat;
extern String g_lon;
extern String g_lang;


// ============================================================================
// PROTOTIPI
// ============================================================================
bool fetchSun();
void pageSun();


// ============================================================================
// CACHE LOCALE
// ============================================================================
static String sun_rise = "";
static String sun_set  = "";
static String sun_noon = "";
static String sun_len  = "";
static String sun_civB = "";
static String sun_civE = "";


// ============================================================================
// ESTRAE HH:MM da un timestamp ISO8601
// ============================================================================
static inline String isoToHM(const String& iso) {
    int t = iso.indexOf('T');
    if (t < 0 || t + 5 >= iso.length()) return "--:--";
    return iso.substring(t + 1, t + 6);
}


// ============================================================================
// my_timegm() — implementazione locale di timegm() per ESP32
// ============================================================================
static time_t my_timegm(struct tm* t)
{
    char* oldTZ = getenv("TZ");

    setenv("TZ", "UTC", 1);
    tzset();

    time_t out = mktime(t);

    if (oldTZ)
        setenv("TZ", oldTZ, 1);
    else
        unsetenv("TZ");

    tzset();
    return out;
}


// ============================================================================
// parseISOtoTimeT() — ISO8601 UTC → time_t (UTC)
// ============================================================================
static bool parseISOtoTimeT(const String& iso, time_t &out)
{
    if (iso.length() < 19) return false;

    struct tm tt = {};

    tt.tm_year = iso.substring(0,4).toInt() - 1900;
    tt.tm_mon  = iso.substring(5,7).toInt() - 1;
    tt.tm_mday = iso.substring(8,10).toInt();
    tt.tm_hour = iso.substring(11,13).toInt();
    tt.tm_min  = iso.substring(14,16).toInt();
    tt.tm_sec  = iso.substring(17,19).toInt();

    out = my_timegm(&tt);
    return true;
}


// ============================================================================
// extractJSON() — "key":"value"
// ============================================================================
static bool extractJSON(const String& body,
                        const char* key,
                        String& out)
{
    String k = String("\"") + key + "\"";

    int p = indexOfCI(body, k);
    if (p < 0) return false;

    p = body.indexOf(':', p);
    if (p < 0) return false;

    int q1 = body.indexOf('"', p + 1);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0) return false;

    out = body.substring(q1 + 1, q2);
    return true;
}


// ============================================================================
// fetchSun() — scarica dati e calcola durata luce locale
// ============================================================================
bool fetchSun()
{
    sun_rise = sun_set = sun_noon = sun_len = sun_civB = sun_civE = "";

    if (g_lat.isEmpty() || g_lon.isEmpty())
        return false;

    String url =
      "https://api.sunrise-sunset.org/json?lat=" + g_lat +
      "&lng=" + g_lon +
      "&formatted=0";

    String body;
    if (!httpGET(url, body, 10000))
        return false;

    String sr, ss, sn, cb, ce;

    if (!extractJSON(body, "sunrise", sr)) return false;
    if (!extractJSON(body, "sunset",  ss)) return false;

    extractJSON(body, "solar_noon",           sn);
    extractJSON(body, "civil_twilight_begin", cb);
    extractJSON(body, "civil_twilight_end",   ce);

    sun_rise = isoToHM(sr);
    sun_set  = isoToHM(ss);
    sun_noon = sn.length() ? isoToHM(sn) : "--:--";
    sun_civB = cb.length() ? isoToHM(cb) : "--:--";
    sun_civE = ce.length() ? isoToHM(ce) : "--:--";

    // --- calcolo durata luce locale ---
    time_t tRise, tSet;

    if (parseISOtoTimeT(sr, tRise) &&
        parseISOtoTimeT(ss, tSet) &&
        tSet > tRise)
    {
        long diff = tSet - tRise;
        int h = diff / 3600;
        int m = (diff % 3600) / 60;

        char buf[16];
        snprintf(buf, sizeof(buf), "%02dh %02dm", h, m);
        sun_len = buf;
    }
    else {
        sun_len = "--h --m";
    }

    return true;
}


// ============================================================================
// drawSunRow() — label sinistra + valore destra + linea
// ============================================================================
static inline void drawSunRow(const char* label,
                              const String& val,
                              int& y)
{
    const int SZ = TEXT_SCALE + 1;
    const int RH = BASE_CHAR_H * SZ + 10;

    gfx->setTextSize(SZ);

    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->setCursor(PAGE_X, y);
    gfx->print(label);

    gfx->setTextColor(COL_ACCENT2, COL_BG);
    int w = val.length() * BASE_CHAR_W * SZ;
    int x = 480 - w - PAGE_X;
    gfx->setCursor(x, y);
    gfx->print(val);

    y += RH;
    gfx->drawLine(PAGE_X, y, 480 - PAGE_X, y, COL_ACCENT1);
    y += 6;
}


// ============================================================================
// pageSun() — rendering finale
// ============================================================================
void pageSun()
{
    bool it = (g_lang == "it");

    drawHeader(it ? "Ore di luce oggi" : "Today's daylight");
    int y = PAGE_Y + 20;

    if (sun_rise.isEmpty() || sun_set.isEmpty()) {
        drawBoldMain(PAGE_X, y,
                     it ? "Nessun dato disponibile"
                        : "No data available",
                     TEXT_SCALE);
        return;
    }

    const char* L_r = it ? "Alba"          : "Sunrise";
    const char* L_s = it ? "Tramonto"      : "Sunset";
    const char* L_n = it ? "Mezzogiorno"   : "Solar noon";
    const char* L_l = it ? "Durata luce"   : "Day length";
    const char* L_cb= it ? "Civile inizio" : "Civil begin";
    const char* L_ce= it ? "Civile fine"   : "Civil end";

    drawSunRow(L_r,  sun_rise, y);
    drawSunRow(L_s,  sun_set,  y);
    drawSunRow(L_n,  sun_noon, y);
    drawSunRow(L_l,  sun_len,  y);
    drawSunRow(L_cb, sun_civB, y);
    drawSunRow(L_ce, sun_civE, y);
}


