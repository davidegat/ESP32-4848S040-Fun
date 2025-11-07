/*
  ESP32-S3 Panel-4848S040 · Pixel Art Grid + Palette + Clear/Export

  Autore: Davide Nasato (gat)
  Repository: https://github.com/davidegat/ESP32-4848S040-Fun
  Nota: software realizzato anche con l’aiuto di modelli linguistici (LLM)

  Descrizione sintetica del funzionamento
  --------------------------------------
  - Il display 480x480 (ST7701 via RGB) viene usato in portrait.
  - Area sinistra 400x480: griglia "pixel art" di 20x24 celle, cella = 20x20 px.
  - Colonna destra 80x480: palette verticale di 5 colori selezionabili.
  - Barra inferiore (in basso, centrata sulla griglia): pulsanti CLEAR e EXPORT.
      • CLEAR  : pulisce l’intera griglia (riporta tutte le celle a bianco).
      • EXPORT : salva l’area di griglia (400x480) su microSD come JPEG
                 (se disponibile la libreria JPEGENC) oppure come BMP fallback.
  - Il tocco è gestito con “edge detection” e cooldown per evitare ripetizioni.
  - Nessuna modifica al pilotaggio pannello e timing del display.

  Requisiti librerie:
  - Arduino_GFX_Library (display ST7701 via RGB + SWSPI per init)
  - TAMC_GT911 (touch GT911)
  - SD e SPI (core ESP32)
  - JPEGENC (opzionale, per export JPEG; se assente → BMP di fallback)
*/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <SPI.h>
#include <SD.h>

#if __has_include(<JPEGENC.h>)
  #include <JPEGENC.h>
  #define CAM_USE_JPEGENC 1
#else
  #define CAM_USE_JPEGENC 0
#endif

// ----------------------- CONFIGURAZIONE HARDWARE -----------------------
#define I2C_SDA_PIN 19
#define I2C_SCL_PIN 45

#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// --- BUS COMANDI ST7701 (SWSPI) --- (non modificare)
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /*DC*/, 39 /*CS*/,
  48 /*SCK*/, 47 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/
);

// --- BUS PIXEL RGB --- (non modificare)
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

// --- DISPLAY ST7701 --- (non modificare)
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480,
  rgbpanel, 0 /*rotation*/, true /*auto_flush*/,
  bus, GFX_NOT_DEFINED /*RST*/,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// --- TOUCH GT911 ---
#define GT911_INT_PIN  -1
#define GT911_RST_PIN  -1
TAMC_GT911 ts = TAMC_GT911(I2C_SDA_PIN, I2C_SCL_PIN, GT911_INT_PIN, GT911_RST_PIN, 480, 480);

// --- SD CARD (SPI) ---
#define SD_CS   42
#define SD_SCK  48
#define SD_MOSI 47
#define SD_MISO 41
SPIClass sdSPI(FSPI);

// ----------------------- PARAMETRI INTERFACCIA -----------------------
// Area di disegno (griglia)
static const int GRID_W = 400;
static const int GRID_H = 480;

// Colonna palette (destra)
static const int PALETTE_X = 400;
static const int PALETTE_W = 80;

// Barra inferiore con pulsanti centrati
static const int BOTTOM_BAR_H = 48;
static const int BTN_W = 120;
static const int BTN_H = 36;
static const int BTN_GAP = 20;
static const int BTN_Y = GRID_H - (BOTTOM_BAR_H + BTN_H) / 2;
static const int CENTER_X = GRID_W / 2;
static const int BTN1_X = CENTER_X - BTN_W - (BTN_GAP / 2); // CLEAR
static const int BTN2_X = CENTER_X + (BTN_GAP / 2);          // EXPORT

// Griglia 20x24 (celle 20x20 px)
static const int CELL = 20;
static const int COLS = GRID_W / CELL;
static const int ROWS = GRID_H / CELL;

// Palette: 5 colori
static const int PALETTE_KEYS = 5;
static const int KEY_H = 480 / PALETTE_KEYS;

// Colori disponibili
uint16_t palette[PALETTE_KEYS] = {
  RGB565(0,0,0),      // Nero
  RGB565(255,0,0),    // Rosso
  RGB565(0,200,0),    // Verde
  RGB565(0,0,255),    // Blu
  RGB565(255,220,0)   // Giallo
};
int currentColorIndex = 1; // default: Rosso

// Stato celle della griglia
uint16_t cellColor[ROWS][COLS];

// ----------------------- FUNZIONI UTILI GRAFICHE -----------------------
static inline void drawCenteredTextInRect(int rx, int ry, int rw, int rh, const char* txt, uint16_t color, uint8_t size)
{
  // Testo centrato in un rettangolo
  int tw = strlen(txt) * 6 * size;
  int th = 8 * size;
  int cx = rx + (rw - tw) / 2;
  int cy = ry + (rh - th) / 2;
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->setCursor(cx, cy);
  gfx->print(txt);
}

static inline bool inRect(int x, int y, int rx, int ry, int rw, int rh)
{
  // Punto in rettangolo?
  return (x >= rx && x < rx+rw && y >= ry && y < ry+rh);
}

// ----------------------- DISEGNO INTERFACCIA -----------------------
inline void drawGrid()
{
  // Griglia bianca con linee nere ogni 20 px
  gfx->fillRect(0, 0, GRID_W, GRID_H, WHITE);
  for (int x = 0; x <= GRID_W; x += CELL) gfx->drawFastVLine(x, 0, GRID_H, BLACK);
  for (int y = 0; y <= GRID_H; y += CELL) gfx->drawFastHLine(0, y, GRID_W, BLACK);
}

inline void drawBottomBar()
{
  // Barra inferiore + pulsanti centrati
  gfx->fillRect(0, GRID_H - BOTTOM_BAR_H, GRID_W, BOTTOM_BAR_H, gfx->color565(20,20,20));

  gfx->drawRect(BTN1_X, BTN_Y, BTN_W, BTN_H, WHITE);
  gfx->fillRect(BTN1_X+1, BTN_Y+1, BTN_W-2, BTN_H-2, gfx->color565(60,60,60));
  drawCenteredTextInRect(BTN1_X, BTN_Y, BTN_W, BTN_H, "CLEAR", YELLOW, 2);

  gfx->drawRect(BTN2_X, BTN_Y, BTN_W, BTN_H, WHITE);
  gfx->fillRect(BTN2_X+1, BTN_Y+1, BTN_W-2, BTN_H-2, gfx->color565(60,60,60));
  drawCenteredTextInRect(BTN2_X, BTN_Y, BTN_W, BTN_H, "EXPORT", CYAN, 2);
}

inline void drawPalette()
{
  // Colonna destra con 5 “tasti” colore
  gfx->fillRect(PALETTE_X, 0, PALETTE_W, 480, gfx->color565(30,30,30));
  for (int i = 0; i < PALETTE_KEYS; ++i) {
    int y = i * KEY_H;
    gfx->drawRect(PALETTE_X + 6, y + 6, PALETTE_W - 12, KEY_H - 12, WHITE);
    gfx->fillRect(PALETTE_X + 10, y + 10, PALETTE_W - 20, KEY_H - 20, palette[i]);
    // Evidenzia selezione corrente
    if (i == currentColorIndex) {
      gfx->drawRect(PALETTE_X + 4, y + 4, PALETTE_W - 8,  KEY_H - 8,  YELLOW);
      gfx->drawRect(PALETTE_X + 3, y + 3, PALETTE_W - 6,  KEY_H - 6,  YELLOW);
    }
  }
}

inline void drawCell(int r, int c, uint16_t color)
{
  // Riempie l’interno della cella senza coprire le linee di griglia
  int x = c * CELL;
  int y = r * CELL;
  gfx->fillRect(x + 1, y + 1, CELL - 2, CELL - 2, color);
}

inline void fullRedrawFromState()
{
  // Ridisegno completo: griglia, celle, barra e palette
  drawGrid();
  for (int r = 0; r < ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      if (cellColor[r][c] != WHITE) drawCell(r, c, cellColor[r][c]);
  drawBottomBar();
  drawPalette();
}

// ----------------------- AZIONI: CLEAR / EXPORT -----------------------
inline void clearCanvas()
{
  // Reset di tutta la griglia a bianco
  for (int r = 0; r < ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      cellColor[r][c] = WHITE;
  fullRedrawFromState();
}

static inline uint16_t getPixel565FromState(int x, int y)
{
  // Ricostruisce il pixel della griglia (linee nere incluse)
  if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return BLACK;
  if ((x % CELL) == 0 || (y % CELL) == 0) return BLACK; // linee griglia
  int c = x / CELL;
  int r = y / CELL;
  return cellColor[r][c];
}

// Callback di scrittura per JPEGENC (se presente)
#if CAM_USE_JPEGENC
static int jpeg_write_cb(JPEGENC *j, void *pUser, const uint8_t *pBuf, int iLen)
{
  File *pf = (File *)pUser;
  if (!pf || !pf->write(pBuf, iLen)) return 0;
  return iLen;
}
#endif

bool exportJPEGtoSD()
{
  // Salva l’area griglia 400x480 su SD (JPEG se possibile, altrimenti BMP)
  if (!SD.begin(SD_CS, sdSPI)) return false;

  char path[40];
  snprintf(path, sizeof(path), "/pixel_%lu.%s", (unsigned long)millis(), CAM_USE_JPEGENC ? "jpg" : "bmp");
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;

#if CAM_USE_JPEGENC
  // --- Export JPEG tramite JPEGENC (RGB565) ---
  JPEGENC j;
  if (j.open(&jpeg_write_cb, (void *)&f, GRID_W, GRID_H, JPEGENC_PIXEL_RGB565,
             JPEGENC_SUBSAMPLE_420, JPEGENC_QUALITY_HIGH) == 0) {
    f.close();
    return false;
  }
  static uint16_t line[GRID_W];
  for (int y = 0; y < GRID_H; ++y) {
    for (int x = 0; x < GRID_W; ++x) line[x] = getPixel565FromState(x, y);
    j.addLine((uint8_t *)line);
  }
  j.close();
  f.close();
  return true;

#else
  // --- Export BMP 24-bit (fallback) ---
  const uint32_t w = GRID_W, h = GRID_H;
  const uint32_t rowBytes = ((w * 3 + 3) & ~3);
  const uint32_t pixelData = rowBytes * h;
  const uint32_t fileSize = 54 + pixelData;

  uint8_t hdr[54] = {
    'B','M',
    (uint8_t)(fileSize), (uint8_t)(fileSize>>8), (uint8_t)(fileSize>>16), (uint8_t)(fileSize>>24),
    0,0, 0,0,
    54,0,0,0,
    40,0,0,0,
    (uint8_t)(w), (uint8_t)(w>>8), (uint8_t)(w>>16), (uint8_t)(w>>24),
    (uint8_t)(h), (uint8_t)(h>>8), (uint8_t)(h>>16), (uint8_t)(h>>24),
    1,0, 24,0, 0,0,0,0,
    (uint8_t)(pixelData), (uint8_t)(pixelData>>8), (uint8_t)(pixelData>>16), (uint8_t)(pixelData>>24),
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
  };
  f.write(hdr, 54);

  std::unique_ptr<uint8_t[]> row(new uint8_t[rowBytes]);
  for (int y = h - 1; y >= 0; --y) {
    uint8_t *p = row.get();
    for (int x = 0; x < (int)w; ++x) {
      uint16_t c = getPixel565FromState(x, y);
      uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
      uint8_t g = ((c >> 5) & 0x3F) * 255 / 63;
      uint8_t b = (c & 0x1F) * 255 / 31;
      *p++ = b; *p++ = g; *p++ = r;
    }
    while ((p - row.get()) < (int)rowBytes) *p++ = 0; // padding a 4 byte
    f.write(row.get(), rowBytes);
  }
  f.close();
  return true;
#endif
}

// ----------------------- GESTIONE TOUCH (DEBOUNCE) -----------------------
static bool prevTouched = false;                 // stato tocco precedente
static const uint32_t TAP_COOLDOWN_MS = 700;     // cooldown tap
static uint32_t lastClearMs  = 0;                // ultimo CLEAR
static uint32_t lastExportMs = 0;                // ultimo EXPORT

// ----------------------- SETUP -----------------------
void setup()
{
  // Retroilluminazione
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);

  // Display + Touch
  gfx->begin();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  ts.begin();
  ts.setRotation(0);

  // SD (SPI)
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI);

  // Inizializza griglia a bianco
  for (int r = 0; r < ROWS; ++r)
    for (int c = 0; c < COLS; ++c)
      cellColor[r][c] = WHITE;

  fullRedrawFromState();
}

// ----------------------- LOOP PRINCIPALE -----------------------
void loop()
{
  ts.read();
  bool touchedNow = ts.isTouched;

  // Attiva le azioni solo sul “fronte di discesa” del tocco (tap)
  if (touchedNow && !prevTouched)
  {
    uint16_t tx = ts.points[0].x;
    uint16_t ty = ts.points[0].y;

    // Mappa coordinate GT911 → schermo (portrait)
    uint16_t x = ty;
    uint16_t y = 480 - tx;
    if (x > 479) x = 479;
    if (y > 479) y = 479;

    uint32_t nowMs = millis();

    // Tocco in colonna palette (destra): selezione colore
    if (x >= PALETTE_X) {
      int key = y / KEY_H;
      if (key >= 0 && key < PALETTE_KEYS && key != currentColorIndex) {
        currentColorIndex = key;
        drawPalette();
      }
    } else {
      // Tocco in barra inferiore: CLEAR / EXPORT
      if (y >= GRID_H - BOTTOM_BAR_H) {
        if (inRect(x, y, BTN1_X, BTN_Y, BTN_W, BTN_H)) {
          if (nowMs - lastClearMs > TAP_COOLDOWN_MS) {
            lastClearMs = nowMs;
            clearCanvas();
          }
        } else if (inRect(x, y, BTN2_X, BTN_Y, BTN_W, BTN_H)) {
          if (nowMs - lastExportMs > TAP_COOLDOWN_MS) {
            lastExportMs = nowMs;

            // Messaggio centrato durante l’export
            gfx->fillRect(0, GRID_H - BOTTOM_BAR_H, GRID_W, BOTTOM_BAR_H, gfx->color565(20,20,20));
            drawCenteredTextInRect(0, GRID_H - BOTTOM_BAR_H, GRID_W, BOTTOM_BAR_H, "Esportazione...", WHITE, 2);

            bool ok = exportJPEGtoSD();

            // Esito centrato, poi ripristino barra
            gfx->fillRect(0, GRID_H - BOTTOM_BAR_H, GRID_W, BOTTOM_BAR_H, gfx->color565(20,20,20));
            drawCenteredTextInRect(0, GRID_H - BOTTOM_BAR_H, GRID_W, BOTTOM_BAR_H,
                                   ok ? "Export OK" : "Export FAIL", ok ? GREEN : RED, 2);
            delay(1200);
            drawBottomBar();
          }
        }
      } else {
        // Disegno nella griglia: toggle colore <-> bianco
        int c = x / CELL;
        int r = y / CELL;
        if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
          uint16_t curr = cellColor[r][c];
          // Se la cella è già colorata -> torna bianca; se è bianca -> colora
          uint16_t next = (curr != WHITE) ? WHITE : palette[currentColorIndex];
          if (curr != next) {
            cellColor[r][c] = next;
            drawCell(r, c, next);
          }
        }
      }
    }
  }

  prevTouched = touchedNow;
  delay(20); // piccoli ritardo per stabilizzare letture touch
}
