#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <QRCodeGenerator.h>   // Libreria Felix Erdmann (API C-style: qrcode_initText, qrcode_getModule)

// ====================== CONFIGURAZIONE HARDWARE ======================
// Touch GT911 su I2C (coerente con i tuoi sketch)
#define I2C_SDA_PIN 19
#define I2C_SCL_PIN 45
#define GT911_INT_PIN  -1
#define GT911_RST_PIN  -1
TAMC_GT911 ts(I2C_SDA_PIN, I2C_SCL_PIN, GT911_INT_PIN, GT911_RST_PIN, 480, 480);

// Retroilluminazione via PWM (LEDC)
#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// Bus comandi ST7701: SWSPI per init comandi (non invia pixel)
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED,  // DC non usato
  39,               // CS
  48,               // SCK
  47,               // MOSI
  GFX_NOT_DEFINED   // MISO non usato
);

// Bus RGB per i dati pixel + timing pannello 480x480
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  /* DE, VSYNC, HSYNC, PCLK */ 18, 17, 16, 21,
  /* R0..R4 */ 11, 12, 13, 14, 0,
  /* G0..G5 */ 8, 20, 3, 46, 9, 10,
  /* B0..B4 */ 4, 5, 6, 7, 15,
  /* hsync_pol, hfp, hpw, hbp */ 1, 10, 8, 50,
  /* vsync_pol, vfp, vpw, vbp */ 1, 10, 8, 20,
  /* pclk_active_neg */ 0,
  /* prefer_speed */ 12000000,
  /* big_endian */ false,
  /* de_idle_high, pclk_idle_high, bounce_buf */ 0, 0, 0
);

// Oggetto display principale 480x480 + init ST7701 type9
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480,
  rgbpanel, 0 /*rotation*/, true /*auto_flush*/,
  bus, GFX_NOT_DEFINED /*RST*/,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// ====================== GENERAZIONE LINK YOUTUBE ======================
// Solo caratteri alfanumerici A–Z, a–z, 0–9 (niente '-' o '_')
static const char ALNUM[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

// Contatore globale dei QR generati (serve per l'easter egg)
static uint32_t g_qrCount = 0;

// Genera ID da 11 caratteri alfanumerici, garantendo mix minimo (1 maiuscola, 1 minuscola, 1 cifra)
static String generateYouTubeID() {
  char id[12];
  for (int i = 0; i < 11; i++)
    id[i] = ALNUM[esp_random() % (sizeof(ALNUM) - 1)];

  bool hasU=false, hasL=false, hasD=false;
  for (int i = 0; i < 11; i++) {
    char c = id[i];
    if (c >= 'A' && c <= 'Z') hasU = true;
    else if (c >= 'a' && c <= 'z') hasL = true;
    else if (c >= '0' && c <= '9') hasD = true;
  }

  auto forceAt = [&](char c) {
    int idx = esp_random() % 11;
    id[idx] = c;
  };
  if (!hasU) forceAt('A' + (esp_random() % 26));
  if (!hasL) forceAt('a' + (esp_random() % 26));
  if (!hasD) forceAt('0' + (esp_random() % 10));

  id[11] = '\0';
  return String(id);
}

// Ritorna il prossimo URL:
// - ogni 5° QR restituisce un vero Rickroll (EASTER EGG)
// - altrimenti genera link completo con ID alfanumerico casuale
static String nextYouTubeURL() {
  g_qrCount++;
  if (g_qrCount % 5 == 0) {
    // EASTER EGG: vero Rickroll ogni 5 generazioni
    return String("https://www.youtube.com/watch?v=xvFZjo5PgG0");
  }
  return String("https://www.youtube.com/watch?v=") + generateYouTubeID();
}

// ====================== RENDER: MOSTRA URL + QR ======================
// Mostra l’URL per 3 s (debug a schermo), poi disegna il QR a pieno schermo (versione 5, ECC medio)
static void drawQRCodeFullScreen(const String &url) {
  // 1) URL in chiaro per 3 secondi
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(1);
  // Stampa con wrap molto semplice
  int x = 10, y = 220;
  const int maxWidth = 460; // 480-20 margini
  int cursor = 0;
  while (cursor < (int)url.length()) {
    int len = 0, w = 0;
    while (cursor + len < (int)url.length() && w < maxWidth) {
      w += 6; // ~6px per char con TextSize=1
      len++;
    }
    gfx->setCursor(x, y);
    gfx->print(url.substring(cursor, cursor + len));
    y += 12;
    cursor += len;
  }
  delay(3000);

  // 2) QR centrato a pieno schermo
  gfx->fillScreen(BLACK);

  const uint8_t version = 5;       // 37x37 moduli (sufficiente per URL brevi)
  const uint8_t ecc     = ECC_MEDIUM;

  QRCode qrcode;
  size_t bufSize = qrcode_getBufferSize(version);
  uint8_t *qrcodeData = (uint8_t*)malloc(bufSize);
  if (!qrcodeData) return;

  qrcode_initText(&qrcode, qrcodeData, version, ecc, url.c_str());

  int qrSize = qrcode.size;
  int scale  = 480 / qrSize;       // scala intera massima
  if (scale < 1) scale = 1;
  int used   = qrSize * scale;
  int offX   = (480 - used) / 2;
  int offY   = (480 - used) / 2;

  for (int y2 = 0; y2 < qrSize; y2++) {
    for (int x2 = 0; x2 < qrSize; x2++) {
      if (qrcode_getModule(&qrcode, x2, y2))
        gfx->fillRect(offX + x2 * scale, offY + y2 * scale, scale, scale, WHITE);
    }
  }

  free(qrcodeData);
}

// ====================== CICLO PRINCIPALE: TOUCH ======================
// Edge-detect sul tocco GT911: ogni tap genera nuovo URL e relativo QR
static bool prevTouched = false;

void setup() {
  Serial.begin(115200); // Debug: stampa gli URL in chiaro

  // Retroilluminazione al massimo
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);

  // Inizializza display e touch
  gfx->begin();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ts.begin();
  ts.setRotation(0);

  // Primo URL+QR all’avvio (conteggiato nel modulo 5 per l’easter egg)
  String url = nextYouTubeURL();
  Serial.println(url);
  drawQRCodeFullScreen(url);
}

void loop() {
  ts.read();
  bool touchedNow = ts.isTouched;

  if (touchedNow && !prevTouched) {
    String url = nextYouTubeURL();     // genera nuovo URL (con easter egg ogni 5)
    Serial.println(url);               // debug su seriale
    drawQRCodeFullScreen(url);         // mostra URL 3s → QR fullscreen
  }

  prevTouched = touchedNow;
  delay(20); // piccolo debounce/pace del loop
}
