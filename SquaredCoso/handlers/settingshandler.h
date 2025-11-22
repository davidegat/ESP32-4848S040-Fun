#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include "globals.h"

// ============================================================================
//  MODULO: SETTINGS
//
//  Gestisce:
//    • ricezione dei valori POST dalla WebUI
//    • validazione e sanitizzazione dati
//    • salvataggio in NVS (Preferences)
//    • caricamento dei valori all'avvio
//    • generazione pagina HTML tramite htmlSettings()
//
// ============================================================================

// Chiavi POST associate alle checkbox delle pagine
static const char* PAGE_KEYS[PAGES] = {
  "p_WEATHER", "p_AIR", "p_CLOCK", "p_CAL", "p_BTC",
  "p_QOD", "p_INFO", "p_COUNT", "p_FX", "p_T24", "p_SUN",
  "p_NEWS"   // ← NUOVA PAGINA
};

extern WebServer web;
extern Preferences prefs;


// ============================================================================
// handleSettings() – Gestione richiesta POST/GET della pagina impostazioni
// ============================================================================
void handleSettings() {

  bool saved = false;

  // Se POST, aggiorno impostazioni
  if (web.method() == HTTP_POST) {

    // --------------------- Parametri base ---------------------
    String city = web.arg("city");
    String lang = web.arg("lang");
    String ics  = web.arg("ics");

    city.trim();
    lang.trim();
    ics.trim();

    if (city.length()) g_city = city;
    g_lang = (lang == "it" || lang == "en") ? lang : "it";
    g_ics  = ics;

    // --------------------- Intervallo cambio pagina ---------------------
    if (web.hasArg("page_s")) {
      long ps = web.arg("page_s").toInt();
      if (ps < 5)   ps = 5;
      if (ps > 600) ps = 600;
      PAGE_INTERVAL_MS = ps * 1000UL;
    }

    // --------------------- Valuta ---------------------
    if (web.hasArg("fiat")) {
      g_fiat = web.arg("fiat");
      g_fiat.trim();
      g_fiat.toUpperCase();
    }

    // --------------------- BTC ---------------------
    if (web.hasArg("btc_owned")) {
      String s = web.arg("btc_owned");
      s.trim();
      g_btc_owned = s.length() ? s.toDouble() : NAN;
    }

    // --------------------- OpenAI ---------------------
    g_oa_key   = sanitizeText(web.arg("openai_key"));
    g_oa_topic = sanitizeText(web.arg("openai_topic"));


    // --------------------- RSS URL ---------------------
    if (web.hasArg("rss_url")) {
        g_rss_url = sanitizeText(web.arg("rss_url"));
        g_rss_url.trim();
    	}
    // --------------------- Countdown (8 elementi) ---------------------
    for (int i = 0; i < 8; i++) {
      char kn[6], kt[6];
      snprintf(kn, sizeof(kn), "cd%dn", i + 1);
      snprintf(kt, sizeof(kt), "cd%dt", i + 1);
      cd[i].name    = sanitizeText(web.arg(kn));
      cd[i].whenISO = sanitizeText(web.arg(kt));
    }

    // --------------------- Pagine visibili ---------------------
    for (int i = 0; i < PAGES; i++)
      g_show[i] = web.hasArg(PAGE_KEYS[i]);

    // Almeno CLOCK deve restare attivo
    bool any = false;
    for (int i = 0; i < PAGES; i++)
      if (g_show[i]) any = true;

    if (!any)
      g_show[P_CLOCK] = true;

    // --------------------- SALVATAGGIO NVS ---------------------
    prefs.begin("app", false);

    prefs.putString("city", g_city);
    prefs.putString("lang", g_lang);
    prefs.putString("ics",  g_ics);
    prefs.putString("lat",  g_lat);
    prefs.putString("lon",  g_lon);
    prefs.putString("rss_url", g_rss_url);

    prefs.putULong("page_ms", PAGE_INTERVAL_MS);

    prefs.putString("oa_key",   g_oa_key);
    prefs.putString("oa_topic", g_oa_topic);
    prefs.putString("fiat",     g_fiat);

    for (int i = 0; i < 8; i++) {
      char kn[6], kt[6];
      snprintf(kn, sizeof(kn), "cd%dn", i + 1);
      snprintf(kt, sizeof(kt), "cd%dt", i + 1);
      prefs.putString(kn, cd[i].name);
      prefs.putString(kt, cd[i].whenISO);
    }

    prefs.putUShort("pages_mask", pagesMaskFromArray());
    prefs.end();

    ensureCurrentPageEnabled();
    g_dataRefreshPending = true;

    saved = true;
  }

  // Risposta pagina impostazioni
  web.send(200, "text/html; charset=utf-8", htmlSettings(saved, ""));
}


// ============================================================================
// loadAppConfig() – Caricamento iniziale da NVS
// ============================================================================
void loadAppConfig() {

  prefs.begin("app", true);

  g_fiat = prefs.getString("fiat", "CHF");
  g_fiat.trim();
  g_fiat.toUpperCase();

  g_city = prefs.getString("city", g_city);
  g_lang = prefs.getString("lang", g_lang);
  g_ics  = prefs.getString("ics",  g_ics);
  g_lat  = prefs.getString("lat",  g_lat);
  g_lon  = prefs.getString("lon",  g_lon);

  PAGE_INTERVAL_MS = prefs.getULong("page_ms", PAGE_INTERVAL_MS);

  g_oa_key   = prefs.getString("oa_key",   g_oa_key);
  g_oa_topic = prefs.getString("oa_topic", g_oa_topic);
  g_rss_url = prefs.getString("rss_url", g_rss_url);
  g_rss_url.trim();


  for (int i = 0; i < 8; i++) {
    char kn[6], kt[6];
    snprintf(kn, sizeof(kn), "cd%dn", i + 1);
    snprintf(kt, sizeof(kt), "cd%dt", i + 1);
    cd[i].name    = prefs.getString(kn, "");
    cd[i].whenISO = prefs.getString(kt, "");
  }

  uint16_t mask = prefs.getUShort("pages_mask", 0xFFFF);
  prefs.end();

  // Validazione intervallo
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  if (s < 5)   PAGE_INTERVAL_MS = 5000;
  if (s > 600) PAGE_INTERVAL_MS = 600000;

  // Applica maschera pagine
  pagesArrayFromMask(mask);

  g_oa_key.trim();
  g_oa_topic.trim();
}


// ============================================================================
// saveAppConfig() – Salvataggio manuale su NVS
// ============================================================================
void saveAppConfig() {

  prefs.begin("app", false);

  prefs.putString("fiat", g_fiat);
  prefs.putString("city", g_city);
  prefs.putString("lang", g_lang);
  prefs.putString("ics",  g_ics);
  prefs.putString("lat",  g_lat);
  prefs.putString("lon",  g_lon);

  prefs.putULong("page_ms", PAGE_INTERVAL_MS);

  prefs.putString("oa_key",   g_oa_key);
  prefs.putString("oa_topic", g_oa_topic);

  for (int i = 0; i < 8; i++) {
    char kn[6], kt[6];
    snprintf(kn, sizeof(kn), "cd%dn", i + 1);
    snprintf(kt, sizeof(kt), "cd%dt", i + 1);
    prefs.putString(kn, cd[i].name);
    prefs.putString(kt, cd[i].whenISO);
  }

  prefs.putUShort("pages_mask", pagesMaskFromArray());
  prefs.end();
}

