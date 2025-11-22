#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_chip_info.h>
#include "../handlers/globals.h"

// ============================================================================
//  MODULO: PAGE INFO (Diagnostica dispositivo)
//
//  Mostra:
//    • URL di accesso alla pagina /settings
//    • carico CPU stimato
//    • RAM libera / totale
//    • dimensione sketch / spazio libero
//    • temperatura MCU
//    • numero di pagine abilitate
//    • info firmware / modello
//
//  NOTA: tutte le misure sono “best effort”, compatibili con ESP32-S3.
// ============================================================================

// Prototipo — implementato a fondo file
String formatShortDate(time_t t);

// ======================
// EXTERN DAL MAIN
// ======================
extern Arduino_RGB_Display* gfx;

extern const uint16_t COL_BG;
extern const uint16_t COL_TEXT;
extern const uint16_t COL_ACCENT1;
extern const uint16_t COL_ACCENT2;
extern const uint16_t COL_DIVIDER;
extern const uint16_t COL_HEADER;
extern const uint16_t COL_GOOD;
extern const uint16_t COL_WARN;
extern const uint16_t COL_BAD;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;
extern const int CHAR_H;

extern void drawHeader(const String& title);
extern void drawHLine(int y);
extern String sanitizeText(const String& in);

extern bool g_show[];   // mappa pagine abilitate


// ============================================================================
// BADGE diagnostico (colore basato sul valore 0–100)
//  <40%  → verde
//  40–69 → giallo
//  ≥70   → rosso
// ============================================================================
static inline void drawBadge(int16_t x, int16_t y, uint8_t v) {

  uint16_t col;

  if (v < 40)       col = 0x07E0;   // verde
  else if (v < 70)  col = 0xFFE0;   // giallo
  else              col = 0xF800;   // rosso

  gfx->fillRoundRect(x, y, 28, 18, 4, col);
}


// ============================================================================
// UTILITY – conversione byte → B / KB / MB
// ============================================================================
static String formatBytes(size_t b) {
  char buf[16];

  if (b >= (1 << 20))
    snprintf(buf, sizeof(buf), "%u MB", (unsigned)(b >> 20));
  else if (b >= (1 << 10))
    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(b >> 10));
  else
    snprintf(buf, sizeof(buf), "%u B", (unsigned)b);

  return String(buf);
}


// ============================================================================
// UTILITY – MAC address formato xx:xx:xx:xx:xx:xx
// ============================================================================
static String macStr() {
  uint8_t m[6];
  WiFi.macAddress(m);

  char buf[24];
  snprintf(buf, sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);

  return String(buf);
}


// ============================================================================
// UTILITY – uptime leggibile d hh:mm:ss
// ============================================================================
static String formatUptime() {
  unsigned long sec = millis() / 1000UL;

  unsigned d = sec / 86400UL;
  sec %= 86400UL;

  unsigned h = sec / 3600UL;
  sec %= 3600UL;

  unsigned m = sec / 60UL;

  char buf[20];
  snprintf(buf, sizeof(buf),
           "%ud %02u:%02u:%02u",
           d, h, m, (unsigned)sec);

  return String(buf);
}


// ============================================================================
// STIMA CARICO CPU
//
// Metodo euristico ma sorprendentemente stabile:
//  - esegue un ciclo “vuoto” per 200ms
//  - misura microsecondi reali trascorsi
//  - mappa il risultato in un range 5–99%
// ============================================================================
static uint8_t estimateCPU() {

  const uint32_t msWindow = 200;
  volatile uint32_t sink = 0;

  uint32_t t0 = millis();
  uint32_t u0 = micros();

  while (millis() - t0 < msWindow)
    sink++;

  uint32_t dt = micros() - u0;

  if (dt > 250000) dt = 250000;

  return constrain(map(dt, 100000, 250000, 5, 99), 5, 99);
}


// ============================================================================
// PAGINA INFO / DIAGNOSTICA
// ============================================================================
static void pageInfo() {

  drawHeader("Device info");
  int y = PAGE_Y;

  // -------------------------------------------------------------------------
  // URL della pagina /settings
  // -------------------------------------------------------------------------
  bool sta = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED);

  String url =
    sta
      ? ("http://" + WiFi.localIP().toString() + "/settings")
      : (WiFi.getMode() == WIFI_AP
           ? ("http://" + WiFi.softAPIP().toString() + "/settings")
           : "n/a");

  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print(url);

  y += CHAR_H * 2 + 10;
  drawHLine(y);
  y += 10;

  // -------------------------------------------------------------------------
  // CPU
  // -------------------------------------------------------------------------
  uint8_t cpu = estimateCPU();

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("CPU: ");
  gfx->print(cpu);
  gfx->print("%");

  drawBadge(420, y + (CHAR_H / 2), cpu);

  y += CHAR_H * 2 + 10;

  // -------------------------------------------------------------------------
  // RAM
  // -------------------------------------------------------------------------
  size_t freeHeap  = ESP.getFreeHeap();
  size_t totalHeap = ESP.getHeapSize();

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("RAM: ");
  gfx->print(formatBytes(freeHeap));
  gfx->print(" / ");
  gfx->print(formatBytes(totalHeap));

  uint8_t ramPct =
    (100 * (totalHeap - freeHeap)) / max((size_t)1, totalHeap);

  drawBadge(420, y + (CHAR_H / 2), ramPct);

  y += CHAR_H * 2 + 10;

  // -------------------------------------------------------------------------
  // FLASH: sketch vs spazio libero
  // -------------------------------------------------------------------------
  size_t sk = ESP.getSketchSize();
  size_t fs = ESP.getFreeSketchSpace();

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("Sketch: ");
  gfx->print(formatBytes(sk));
  gfx->print(" / ");
  gfx->print(formatBytes(fs));

  uint8_t skPct =
    (100 * sk) / max((size_t)1, sk + fs);

  drawBadge(420, y + (CHAR_H / 2), skPct);

  y += CHAR_H * 2 + 10;
  drawHLine(y);
  y += 10;

  // -------------------------------------------------------------------------
  // Temperatura MCU
  // -------------------------------------------------------------------------
  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("MCU temp: ");
  gfx->print((int)temperatureRead());
  gfx->print("C");

  drawBadge(420, y + (CHAR_H / 2), 10);

  y += CHAR_H * 2 + 10;

  // -------------------------------------------------------------------------
  // Numero pagine abilitate
  // -------------------------------------------------------------------------
  int enabled = 0;
  for (int i = 0; i < PAGES; i++)
    enabled += g_show[i] ? 1 : 0;

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("Pages: ");
  gfx->print(enabled);
  gfx->print(" / ");
  gfx->print((int)PAGES);

  uint8_t pgPct =
    (100 * enabled) / ((int)PAGES > 0 ? (int)PAGES : 1);

  drawBadge(420, y + (CHAR_H / 2), pgPct);

  y += CHAR_H * 2 + 14;
  drawHLine(y);
  y += 10;

  // -------------------------------------------------------------------------
  // Identità firmware / modello
  // -------------------------------------------------------------------------
  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("Firmware: SquaredCoso by gat");

  y += CHAR_H * 2 + 10;

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("          github.com/davidegat");

  y += CHAR_H * 2 + 10;

  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print("Model:    ESP32-S3 Panel 4848S040");
}


// ============================================================================
// Funzione ausiliaria minimal per data breve gg/mm
// ============================================================================
String formatShortDate(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d/%02d",
           lt.tm_mday, lt.tm_mon + 1);

  return String(buf);
}

