static void handleSettings() {
  bool saved = false;
  String msg = "";
  if (web.method() == HTTP_POST) {
    String city = web.arg("city");
    String lang = web.arg("lang");
    String ics = web.arg("ics");
    city.trim();
    lang.trim();
    ics.trim();
    if (city.length()) g_city = city;
    if (lang != "it" && lang != "en") {
      msg = "Lingua non valida. Uso 'it'.";
      g_lang = "it";
    } else g_lang = lang;
    g_ics = ics;

    if (web.hasArg("page_s")) {
      long ps = web.arg("page_s").toInt();
      if (ps < 5) ps = 5;
      else if (ps > 600) ps = 600;
      PAGE_INTERVAL_MS = (uint32_t)ps * 1000UL;
    }
    if (web.hasArg("btc_owned")) {
      String s = web.arg("btc_owned");
      s.trim();
      if (s.length() == 0) g_btc_owned = NAN;
      else g_btc_owned = s.toDouble();  // HTML usa il punto, corretto
    }

    // Config QOD/OpenAI
    g_oa_key = sanitizeText(web.arg("openai_key"));
    g_oa_topic = sanitizeText(web.arg("openai_topic"));
    g_oa_key.trim();
    g_oa_topic.trim();

    cd[0].name = sanitizeText(web.arg("cd1n"));
    cd[0].whenISO = sanitizeText(web.arg("cd1t"));
    cd[1].name = sanitizeText(web.arg("cd2n"));
    cd[1].whenISO = sanitizeText(web.arg("cd2t"));
    cd[2].name = sanitizeText(web.arg("cd3n"));
    cd[2].whenISO = sanitizeText(web.arg("cd3t"));
    cd[3].name = sanitizeText(web.arg("cd4n"));
    cd[3].whenISO = sanitizeText(web.arg("cd4t"));
    cd[4].name = sanitizeText(web.arg("cd5n"));
    cd[4].whenISO = sanitizeText(web.arg("cd5t"));
    cd[5].name = sanitizeText(web.arg("cd6n"));
    cd[5].whenISO = sanitizeText(web.arg("cd6t"));
    cd[6].name = sanitizeText(web.arg("cd7n"));
    cd[6].whenISO = sanitizeText(web.arg("cd7t"));
    cd[7].name = sanitizeText(web.arg("cd8n"));
    cd[7].whenISO = sanitizeText(web.arg("cd8t"));

    // ---- Leggi checkbox pagine (presenti -> true, assenti -> false) ----
    g_show[P_WEATHER] = web.hasArg("p_WEATHER");
    g_show[P_AIR] = web.hasArg("p_AIR");
    g_show[P_CLOCK] = web.hasArg("p_CLOCK");
    g_show[P_CAL] = web.hasArg("p_CAL");
    g_show[P_BTC] = web.hasArg("p_BTC");
    g_show[P_QOD] = web.hasArg("p_QOD");
    g_show[P_INFO] = web.hasArg("p_INFO");
    g_show[P_COUNT] = web.hasArg("p_COUNT");
    g_show[P_FX]  = web.hasArg("p_FX");
    g_show[P_T24] = web.hasArg("p_T24");
    g_show[P_SUN] = web.hasArg("p_SUN");

    //   g_show[P_PT]      = web.hasArg("p_PT");

    // Safety: se tutte false, abilita almeno l'orologio
    bool any = false;
    for (int i = 0; i < PAGES; i++)
      if (g_show[i]) {
        any = true;
        break;
      }
    if (!any) g_show[P_CLOCK] = true;

    // Reset coords se cambia città
    prefs.begin("app", true);
    String prevCity = prefs.getString("city", g_city);
    prefs.end();
    if (prevCity != g_city) {
      g_lat = "";
      g_lon = "";
    }

    // Persisti tutto incluso mask pagine
    prefs.begin("app", false);
    prefs.putString("city", g_city);
    prefs.putString("lang", g_lang);
    prefs.putString("ics", g_ics);
    prefs.putString("lat", g_lat);
    prefs.putString("lon", g_lon);
    prefs.putULong("page_ms", PAGE_INTERVAL_MS);
    prefs.putString("oa_key", g_oa_key);
    prefs.putString("oa_topic", g_oa_topic);
    for (int i = 0; i < 8; i++) {
      char kn[6], kt[6];
      snprintf(kn, sizeof(kn), "cd%dn", i + 1);
      snprintf(kt, sizeof(kt), "cd%dt", i + 1);
      prefs.putString(kn, cd[i].name);
      prefs.putString(kt, cd[i].whenISO);
    }
    uint16_t mask = pagesMaskFromArray();
    prefs.putUShort("pages_mask", mask);
    prefs.end();

    ensureCurrentPageEnabled();
    g_dataRefreshPending = true;
    saved = true;
  }
  web.send(200, "text/html; charset=utf-8", htmlSettings(saved));
}

// =========================== Load/Save Config ================================
static void loadAppConfig() {
  prefs.begin("app", true);
  g_city = prefs.getString("city", g_city);
  g_lang = prefs.getString("lang", g_lang);
  g_ics = prefs.getString("ics", g_ics);
  g_lat = prefs.getString("lat", g_lat);
  g_lon = prefs.getString("lon", g_lon);
  uint64_t raw = prefs.getULong64("btc_owned", 0xFFFFFFFFFFFFFFFFULL);
  if (raw == 0xFFFFFFFFFFFFFFFFULL) {
    g_btc_owned = NAN;
  } else {
    memcpy(&g_btc_owned, &raw, sizeof(double));
  }

  PAGE_INTERVAL_MS = prefs.getULong("page_ms", PAGE_INTERVAL_MS);

  g_oa_key = prefs.getString("oa_key", g_oa_key);
  g_oa_topic = prefs.getString("oa_topic", g_oa_topic);

  cd[0].name = prefs.getString("cd1n", "");
  cd[0].whenISO = prefs.getString("cd1t", "");
  cd[1].name = prefs.getString("cd2n", "");
  cd[1].whenISO = prefs.getString("cd2t", "");
  cd[2].name = prefs.getString("cd3n", "");
  cd[2].whenISO = prefs.getString("cd3t", "");
  cd[3].name = prefs.getString("cd4n", "");
  cd[3].whenISO = prefs.getString("cd4t", "");
  cd[4].name = prefs.getString("cd5n", "");
  cd[4].whenISO = prefs.getString("cd5t", "");
  cd[5].name = prefs.getString("cd6n", "");
  cd[5].whenISO = prefs.getString("cd6t", "");
  cd[6].name = prefs.getString("cd7n", "");
  cd[6].whenISO = prefs.getString("cd7t", "");
  cd[7].name = prefs.getString("cd8n", "");
  cd[7].whenISO = prefs.getString("cd8t", "");

  uint16_t mask = prefs.getUShort("pages_mask", 0x01FF /* 9 bit a 1: tutte attive */);
  prefs.end();

  // Clamp intervallo
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  if (s < 5) PAGE_INTERVAL_MS = 5000;
  else if (s > 600) PAGE_INTERVAL_MS = 600000;

  // Applica mask pagine
  pagesArrayFromMask(mask);

  g_oa_key.trim();
  g_oa_topic.trim();
}
static void saveAppConfig() {
  prefs.begin("app", false);
  prefs.putString("city", g_city);
  prefs.putString("lang", g_lang);
  prefs.putString("ics", g_ics);
  prefs.putString("lat", g_lat);
  prefs.putString("lon", g_lon);
  uint64_t raw;
  memcpy(&raw, &g_btc_owned, sizeof(double));
  prefs.putULong64("btc_owned", raw);

  prefs.putULong("page_ms", PAGE_INTERVAL_MS);

  prefs.putString("oa_key", g_oa_key);
  prefs.putString("oa_topic", g_oa_topic);

  for (int i = 0; i < 8; i++) {
    char kn[6], kt[6];
    snprintf(kn, sizeof(kn), "cd%dn", i + 1);
    snprintf(kt, sizeof(kt), "cd%dt", i + 1);
    prefs.putString(kn, cd[i].name);
    prefs.putString(kt, cd[i].whenISO);
  }

  uint16_t mask = pagesMaskFromArray();
  prefs.putUShort("pages_mask", mask);
  prefs.end();
}