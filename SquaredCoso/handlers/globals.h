#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// ============================================================================
//  MODULO: GLOBALS
//
//  Contiene:
//    • dichiarazioni globali condivise tra tutti i moduli
//    • enum pagine e struttura countdown
//    • variabili configurazione (lingua, città, fiat, API key, ecc.)
//    • funzioni per rotazione pagine
//
//  NOTA IMPORTANTE
//  ----------------
//  Le variabili sono SOLO dichiarate qui.
//  La loro DEFINIZIONE è nel file principale .ino
//
// ============================================================================


// ============================================================================
// STRINGA → sanitizzazione testo (HTML / UI)
// ============================================================================
String sanitizeText(const String& in);

// interfaccia HTML: firma corretta con due parametri.
// (nel .ino msg ha un default, quindi QUI non va ripetuto)
String htmlSettings(bool saved, const String& msg);


// ============================================================================
// ENUM: PAGINE DISPONIBILI (ordine di rotazione)
// ============================================================================
enum Page {
  P_WEATHER = 0,
  P_AIR,
  P_CLOCK,
  P_CAL,
  P_BTC,
  P_QOD,
  P_INFO,
  P_COUNT,
  P_FX,
  P_T24,
  P_SUN,
  P_NEWS,
  PAGES          // numero totale pagine
};


// ============================================================================
// STRUCT COUNTDOWN
// ============================================================================
struct CDEvent {
  String name;       // nome visualizzato
  String whenISO;    // data in formato "YYYY-MM-DD HH:MM"
};

extern CDEvent cd[8];   // array countdown globali


// ============================================================================
// VARIABILI GLOBALI DI CONFIGURAZIONE (DEFINITE NEL .INO)
// ============================================================================
extern String g_city;          // città per meteo
extern String g_lang;          // lingua interfaccia ("it" / "en")
extern String g_ics;           // URL calendario ICS
extern String g_lat;           // latitudine
extern String g_lon;           // longitudine
extern uint32_t PAGE_INTERVAL_MS; // intervallo rotazione pagine

extern String g_from_station;  // stazione partenza (pagina treni)
extern String g_to_station;    // stazione arrivo

extern double g_btc_owned;     // BTC posseduti
extern String g_fiat;          // valuta fiat (CHF, EUR, USD...)

extern String g_oa_key;        // OpenAI key
extern String g_oa_topic;      // argomento QOD

extern String g_rss_url;

bool geocodeIfNeeded();        // forward dichiarato qui


// ============================================================================
// PAGINE / ROTAZIONE
// ============================================================================
extern bool g_show[PAGES];     // pagine abilitate
extern int g_page;             // pagina corrente
extern uint16_t g_air_bg;
uint16_t pagesMaskFromArray(); // array → bitmask
void pagesArrayFromMask(uint16_t m); // bitmask → array
int firstEnabledPage();        // prima pagina disponibile
bool advanceToNextEnabled();   // passa alla prossima pagina visibile
void ensureCurrentPageEnabled(); // evita pagina disabilitata


// ============================================================================
// FLAG: RICHIESTA DI REFRESH DATI
// ============================================================================
extern volatile bool g_dataRefreshPending;

#endif // GLOBALS_H

