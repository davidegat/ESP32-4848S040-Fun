/*
  Pongino – ESP32-S3 4848S040 (ST7701 + GT911)
  ... (descrizione invariata)
*/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <math.h>

// === Splash image ===
#include "Pongino.h"   // definisce PONGINO_WIDTH/HEIGHT e const uint16_t Pongino[] PROGMEM

// ---------- BUS SWSPI per init ST7701 (come OraQuadra) ----------
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /* DC */, 39 /* CS */,
  48 /* SCK */, 47 /* MOSI */, GFX_NOT_DEFINED /* MISO */
);

// ---------- RGB Panel (come OraQuadra) ----------
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
  8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
  4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
  1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
  0 /* pclk_active_neg */, 12000000 /* prefer_speed */, false /* bigEndian */,
  0 /* de_idle_high */, 0 /* pclk_idle_high */, 0 /* bounce_buffer */
);

// ---------- Display ST7701 type9 (come OraQuadra) ----------
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0 /* rotation */, true /* auto_flush */,
  bus, GFX_NOT_DEFINED /* RST */,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// ---------- Backlight ----------
#define GFX_BL 38

// ---------- Touch GT911 (come OraQuadra) ----------
#define I2C_SDA_PIN 19
#define I2C_SCL_PIN 45
#define TOUCH_INT   -1
#define TOUCH_RST   -1
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

TAMC_GT911 ts(I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_INT, TOUCH_RST, 480, 480);

// ---------- Costanti UI/Gioco ----------
static const int   SCREEN_W = 480;
static const int   SCREEN_H = 480;
static const uint16_t COL_BG     = 0x0000;
static const uint16_t COL_PADDLE_DEF = 0xFFFF;
static const uint16_t COL_PEND_DEF   = 0xF800;
static const uint16_t COL_BALL   = 0xFFFF;
static const uint16_t COL_TEXT   = 0xFFFF;
static const uint16_t COL_BRICK1 = 0xF800;
static const uint16_t COL_BRICK2 = 0x07E0;
static const uint16_t COL_BRICK3 = 0x001F;
static const uint16_t COL_BRICK4 = 0xFFE0;
static const uint16_t COL_BRICK5 = 0xF81F;

static const uint16_t COL_YELLOW = 0xFFE0;
static const uint16_t COL_PURPLE = 0x780F;
static const uint16_t COL_CYAN   = 0x07FF;
static const uint16_t COL_MAG    = 0xF81F;
static const uint16_t COL_ORANGE = 0xFD20;
static const uint16_t COL_GRAYL  = 0xC618;
static const uint16_t COL_HINT   = 0x7BEF;

static uint16_t COL_PADDLE = COL_PADDLE_DEF;
static uint16_t COL_PEND   = COL_PEND_DEF;

static const int   FRAME_INTERVAL_MS = 11;
static const int   PADDLE_W_BASE = 90;
static const int   PADDLE_H = 12;
static const int   PADDLE_Y = SCREEN_H - 40;
static const int   PADDLE_SPEED = 8;

static const int   PADDLE_W_BUFF   = (int)(PADDLE_W_BASE * 1.3f + 0.5f);
static const int   PADDLE_W_DEBUFF = (PADDLE_W_BASE / 2 > 10) ? (PADDLE_W_BASE / 2) : 10;

// === Palla ===
static const int   BALL_SIZE = 10;
static const float BALL_SPEED_INIT = 3.0f;
static const float BALL_SPEED_INC  = 0.04f;
static const float BALL_SPEED_MAX  = 6.0f;

static const int BRICK_ROWS = 6;
static const int BRICK_COLS = 8;
static const int BRICK_PAD  = 4;
static const int BRICK_TOP  = 60;
static const int BRICK_H    = 20;
static const int BRICK_AREA_W = SCREEN_W - BRICK_PAD * (BRICK_COLS + 1);
static const int BRICK_W    = BRICK_AREA_W / BRICK_COLS;

// ---------- Sfondo stellato ----------
static const int STAR_COUNT = 70;
struct Star { uint16_t x,y; uint16_t color; uint8_t size; };
Star stars[STAR_COUNT];
static const uint16_t STAR_COLORS[3] = { 0x2104, 0x4208, 0x6318 };

// ---------- Particelle esplosione ----------
static const int MAX_PARTICLES = 160;
struct Particle {
  float x,y,vx,vy;
  uint8_t life;
  uint16_t color;
  bool alive;
  int px,py;
  uint8_t sz;
  bool oobOnly;
  bool behindPaddle;
};
Particle particles[MAX_PARTICLES];

// ---------- Stati ----------
struct BallS { float x, y, vx, vy; };
struct PaddleS { int x, y, w, h; };
struct Brick { int x, y, w, h; bool alive; uint16_t color; };

PaddleS paddle;
BallS   ball;
Brick   bricks[BRICK_ROWS * BRICK_COLS];
bool    brickHasMissile[BRICK_ROWS * BRICK_COLS];
int     bricksAlive = 0;
int     score = 0;

bool    ballOnPaddle = true;
int     lives = 3;
bool    victory = false;
bool    gameOver = false;
bool    gameOverDrawn = false;

// ---------- Tracce grafiche & hit count ----------
static uint32_t brickCrackSeed[BRICK_ROWS * BRICK_COLS]; // 0 = nessuna crepa
static uint8_t  brickHits[BRICK_ROWS * BRICK_COLS];

// ---------- Palette handling (opzionale) ----------
static uint16_t brickOrigColor[BRICK_ROWS * BRICK_COLS];
static uint8_t  brickPalIndex[BRICK_ROWS * BRICK_COLS]; // 0..4, r%5
static const uint16_t ALT_PAL[5] = { 0x07FF, 0xF81F, 0xFFE0, 0x07E0, 0xFD20 };

enum PalPhase { PAL_ORIG_FADE, PAL_XFADE_TO_ALT, PAL_ALT_FADE, PAL_XFADE_TO_ORIG };
static PalPhase palPhase = PAL_ORIG_FADE;
static uint8_t  palXAlpha = 0;
static bool     palAltActive = false;
static int      palFadeCycles = 0;
static bool     hoverHitTop = false;

// ---------- Bonus ----------
struct Bonus {
  bool active;
  int x,y,w,h;
  int px,py;
  int vy;
  uint16_t color;
};
Bonus bonusDrop = { false, 0,0, 18,14, 0,0, 3, 0x07FF };
uint32_t lastBonusMs = 0;
const uint32_t BONUS_PERIOD_MS = 8000;

Bonus pdownDrop = { false, 0,0, 18,14, 0,0, 3, 0xF81F };
uint32_t lastPowerDownMs = 0;
const uint32_t PDOWN_PERIOD_MS = 16000;

static const int DROPS_MIN_X_SEP = 80;

// Durata effetti
const uint32_t PADDLE_BUFF_MS  = 15000;
bool      paddleBuffActive = false;
uint32_t  paddleBuffEndMs  = 0;

// === Stato dimensione paddle (anti-glitch) ===
enum PaddleSizeState { PS_NORMAL, PS_BUFF, PS_DEBUFF };
static PaddleSizeState paddleSizeState = PS_NORMAL;

// FX lampeggio gemma
static bool     bonusBlinkToggle = false;
static uint32_t bonusBlinkLastMs = 0;
static const uint32_t BONUS_BLINK_MS = 160;

// ---------- Missili ----------
struct Missile {
  bool active;
  int x,y,w,h;
  int px,py;
  int vy;
  uint16_t color;
};
static const int MAX_MISSILES = 10;
Missile missiles[MAX_MISSILES];

// ---------- FX lampeggio paddle ----------
static bool     paddleBlinkFxActive = false;
static uint32_t paddleBlinkEndMs    = 0;
static uint32_t paddleBlinkLastMs   = 0;
static const uint32_t PADDLE_BLINK_MS   = 90;
static const uint32_t PADDLE_BLINK_DUR  = 2600;
static uint8_t paddleBlinkStep = 0;

// ---------- HUD anti-flicker ----------
static int lastScore = -1;
static int lastLives = -1;
static uint32_t lastHUDms = 0;

// ---------- Hover bricks ----------
static int8_t   BRICK_HOVER_DELTA = 0;
static int8_t   BRICK_HOVER_DIR   = 1;
static uint32_t BRICK_HOVER_LAST  = 0;
static const uint32_t BRICK_HOVER_STEP_MS = 35;
static const int8_t   BRICK_HOVER_MAX     = 22;
static bool     BRICK_NEED_REPAINT = false;

// ---------- Shuffle periodico ----------
static uint32_t lastShuffleMs = 0;
static uint32_t shuffleMsgUntilMs = 0;

// ---------- Quad ----------
enum Quad { Q_NONE, Q_TL, Q_TR, Q_BL, Q_BR };

// ===== Utilità colore =====
static inline uint16_t clamp565(int r,int g,int b){
  if (r<0) r=0; if(r>31) r=31;
  if (g<0) g=0; if(g>63) g=63;
  if (b<0) b=0; if(b>31) b=31;
  return (uint16_t)((r<<11) | (g<<5) | b);
}
static inline uint16_t shade565(uint16_t c, int8_t d){
  int r = (c>>11) & 0x1F;
  int g = (c>>5)  & 0x3F;
  int b =  c      & 0x1F;
  r += d; g += (d*6)/5; b += d;
  return clamp565(r,g,b);
}
static inline uint16_t gray565(uint8_t v){
  int r = (v * 31) / 255;
  int g = (v * 63) / 255;
  int b = (v * 31) / 255;
  return (uint16_t)((r<<11)|(g<<5)|b);
}
static inline uint16_t blend565(uint16_t a, uint16_t b, uint8_t t){
  int ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
  int br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
  int rr = ar + ((br-ar)*t + 127)/255;
  int gg = ag + ((bg-ag)*t + 127)/255;
  int bb2= ab + ((bb-ab)*t + 127)/255;
  return (uint16_t)((rr<<11)|(gg<<5)|bb2);
}

// ===== RNG locale per crepe deterministiche =====
static inline uint32_t rngNext(uint32_t &s){ s = s*1664525u + 1013904223u; return s; }
static inline int rngRange(uint32_t &s, int lo, int hi){ // [lo,hi]
  uint32_t r = rngNext(s);
  return lo + (int)(r % (uint32_t)(hi - lo + 1));
}

// ===== Helpers missili/powerup richiesti =====
static inline int missilesActiveCount() {
  int n=0; for (int i=0;i<MAX_MISSILES;++i) if (missiles[i].active) ++n;
  return n;
}
static inline bool anyPickupActive() { return bonusDrop.active || pdownDrop.active; }

// ----- Disegni base -----
static inline void drawPaddleRounded(const PaddleS &p) {
  const int r = p.h>>1;
  int coreX = p.x + r;
  int coreW = p.w - (r<<1);
  if (coreW < 0) coreW = 0;
  gfx->fillCircle(p.x + r,       p.y + r, r, COL_PEND);
  gfx->fillCircle(p.x + p.w - r, p.y + r, r, COL_PEND);
  if (coreW > 0) gfx->fillRect(coreX, p.y, coreW, p.h, COL_PADDLE);
}
static inline void drawBallNow() {
  const int r = BALL_SIZE>>1;
  gfx->fillCircle((int)ball.x + r, (int)ball.y + r, r, COL_BALL);
}
static inline bool rectIntersect(int ax,int ay,int aw,int ah, int bx,int by,int bw,int bh) {
  return !(bx > ax+aw  || bx+bw < ax || by > ay+ah || by+bh < ay);
}
static inline bool rectIntersectInflated(int ax,int ay,int aw,int ah, int bx,int by,int bw,int bh, int inf) {
  return rectIntersect(ax, ay, aw, ah, bx - inf, by - inf, bw + 2*inf, bh + 2*inf);
}

// ---------- Stelle ----------
static inline void drawStar(const Star &s) {
  if (s.size == 1) gfx->drawPixel(s.x, s.y, s.color);
  else             gfx->fillRect(s.x, s.y, s.size, s.size, s.color);
}
static void initStars() {
  for (int i=0;i<STAR_COUNT;++i) {
    stars[i].x = random(0, SCREEN_W);
    stars[i].y = random(0, SCREEN_H);
    stars[i].color = STAR_COLORS[random(0,3)];
    stars[i].size = (random(0,100) < 80) ? 1 : 2;
  }
}
static inline void drawStars() { for (int i=0;i<STAR_COUNT;++i) drawStar(stars[i]); }

// ---------- Clear con ripristino stelle ----------
static inline void clearRegion(int x,int y,int w,int h) {
  if (w<=0 || h<=0) return;
  if (x<0) { w += x; x = 0; }
  if (y<0) { h += y; y = 0; }
  if (x+w>SCREEN_W) w = SCREEN_W - x;
  if (y+h>SCREEN_H) h = SCREEN_H - y;
  if (w<=0 || h<=0) return;
  gfx->fillRect(x,y,w,h,COL_BG);
  for (int i=0;i<STAR_COUNT;++i) {
    if (stars[i].x >= x && stars[i].x < x+w && stars[i].y >= y && stars[i].y < y+h) {
      drawStar(stars[i]);
    }
  }
}

// ---------- Safe clear ----------
static inline void redrawBricksInRegion(int rx,int ry,int rw,int rh);
static inline void safeClearRegion(int x,int y,int w,int h, bool redrawBricks=true) {
  const int px = paddle.x, py = paddle.y, pw = paddle.w, ph = paddle.h;
  const int bx = (int)ball.x, by = (int)ball.y, bw = BALL_SIZE, bh = BALL_SIZE;

  const bool hitPaddle = rectIntersect(x,y,w,h, px,py,pw,ph);
  const bool hitBall   = rectIntersect(x,y,w,h, bx,by,bw,bh);

  clearRegion(x,y,w,h);
  if (redrawBricks) redrawBricksInRegion(x,y,w,h);

  if (hitPaddle) drawPaddleRounded(paddle);
  if (hitBall)   drawBallNow();
}

// ---------- Pulizie estese mirate ----------
static inline void clearPaddleRegionExpanded(int x,int y,int w,int h) { clearRegion(x-3, y-3, w+6, h+6); }
static inline void clearBallRegionExpanded(int x,int y) { clearRegion(x-2, y-2, BALL_SIZE+4, BALL_SIZE+4); }
static inline void clearPaddleSafeBand() {
  const int bandH = 80;
  int bandY = max(0, PADDLE_Y - (bandH/2));
  clearRegion(0, bandY, SCREEN_W, bandH);
}

// ---------- Bricks (3D) + crepe random ----------
static inline void drawCrackOverlayRandom(int x,int y,int w,int h, uint32_t seed) {
  if (seed == 0) return;
  uint32_t s = seed;
  const uint16_t cc = 0x0000; // nero

  const int segs = 12 + (rngRange(s,0,12)); // 12..24
  for (int i=0;i<segs;i++) {
    const int x0 = x + 3 + rngRange(s,0,max(1,w-6));
    const int y0 = y + 3 + rngRange(s,0,max(1,h-6));
    const int dx = rngRange(s,-(w/3), (w/3));
    const int dy = rngRange(s,-(h/3), (h/3));
    const int x1 = x0 + dx/2;
    const int y1 = y0 + dy/2;
    gfx->drawLine(x0, y0, x1, y1, cc);

    if ((rngNext(s) & 1u) == 0u) {
      const int ox = ((int)(rngNext(s)%3))-1; // -1..1
      const int oy = ((int)(rngNext(s)%3))-1;
      gfx->drawLine(x0+ox, y0+oy, x1+ox, y1+oy, cc);
    }

    if ((rngNext(s) & 3u) == 0u) {
      const int x2 = x1 + rngRange(s,-6,6);
      const int y2 = y1 + rngRange(s,-6,6);
      gfx->drawLine(x1, y1, x2, y2, cc);
      gfx->drawPixel(x1+1, y1, cc);
      gfx->drawPixel(x1, y1+1, cc);
    }
  }
}

static inline uint16_t currentBrickBaseColor(int idx){
  const uint16_t fromC = brickOrigColor[idx];
  const uint8_t  pi    = brickPalIndex[idx] % 5;
  const uint16_t toC   = ALT_PAL[pi];

  switch (palPhase) {
    case PAL_ORIG_FADE:       return fromC;
    case PAL_ALT_FADE:        return toC;
    case PAL_XFADE_TO_ALT:    return blend565(fromC, toC, palXAlpha);
    case PAL_XFADE_TO_ORIG:   return blend565(toC, fromC, palXAlpha);
    default:                  return fromC;
  }
}
static inline void drawBrick3D_at(int x,int y,int w,int h, uint16_t base, int8_t hover){
  const uint16_t baseShade = shade565(base, hover);
  gfx->fillRect(x, y, w, h, baseShade);

  const uint16_t hi1 = shade565(baseShade, +12);
  const uint16_t hi2 = shade565(baseShade, +8);
  const uint16_t hi3 = shade565(baseShade, +5);
  const uint16_t lo1 = shade565(baseShade, -12);
  const uint16_t lo2 = shade565(baseShade, -8);
  const uint16_t lo3 = shade565(baseShade, -5);

  gfx->drawFastHLine(x+1, y+1, w-2, hi1);
  gfx->drawFastHLine(x+1, y+2, w-2, hi2);
  gfx->drawFastHLine(x+1, y+3, w-2, hi3);
  gfx->drawFastVLine(x+1, y+1, h-2, hi1);
  gfx->drawFastVLine(x+2, y+1, h-2, hi2);
  gfx->drawFastVLine(x+3, y+1, h-2, hi3);

  gfx->drawFastHLine(x+1, y+h-2, w-2, lo1);
  gfx->drawFastHLine(x+1, y+h-3, w-2, lo2);
  gfx->drawFastHLine(x+1, y+h-4, w-2, lo3);
  gfx->drawFastVLine(x+w-2, y+1, h-2, lo1);
  gfx->drawFastVLine(x+w-3, y+1, h-2, lo2);
  gfx->drawFastVLine(x+w-4, y+1, h-2, lo3);

  if (w>6 && h>6) {
    const uint16_t face = shade565(baseShade, -4);
    gfx->fillRect(x+4, y+4, w-8, h-8, face);
    for (int d=0; d<min(w,h)/4; ++d) gfx->drawPixel(x+4+d, y+4+d, shade565(baseShade, +14));
  }
}
static void initBricks() {
  bricksAlive = 0;
  const uint16_t pal[5] = {COL_BRICK1, COL_BRICK2, COL_BRICK3, COL_BRICK4, COL_BRICK5};
  for (int i=0;i<BRICK_ROWS*BRICK_COLS;++i) {
    brickHasMissile[i] = false;
    brickCrackSeed[i]  = 0;
    brickHits[i]       = 0;
  }

  int counter = 0;
  for (int r = 0; r < BRICK_ROWS; ++r) {
    for (int c = 0; c < BRICK_COLS; ++c) {
      const int idx = r*BRICK_COLS + c;
      Brick &b = bricks[idx];
      b.w = BRICK_W; b.h = BRICK_H;
      b.x = BRICK_PAD + c * (BRICK_W + BRICK_PAD);
      b.y = BRICK_TOP + r * (BRICK_H + BRICK_PAD);
      b.alive = true;
      const uint16_t col = pal[r % 5];
      b.color = col;
      brickOrigColor[idx] = col;
      brickPalIndex[idx]  = (uint8_t)(r % 5);

      // Missili su circa 1/7 dei mattoni
      if (counter % 7 == 0) brickHasMissile[idx] = true;

      counter++;
      bricksAlive++;
    }
  }

  palPhase = PAL_ORIG_FADE;
  palAltActive = false;
  palXAlpha = 0;
  palFadeCycles = 0;
  hoverHitTop = false;
}
static inline void drawBricks(bool fullPaint = false) {
  for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i) {
    Brick &b = bricks[i];
    if (b.alive) {
      const uint16_t base = currentBrickBaseColor(i);
      drawBrick3D_at(b.x, b.y, b.w, b.h, base, BRICK_HOVER_DELTA);
      if (brickCrackSeed[i]) drawCrackOverlayRandom(b.x, b.y, b.w, b.h, brickCrackSeed[i]);
    } else if (fullPaint) {
      clearRegion(b.x, b.y, b.w, b.h);
    }
  }
}
static inline void redrawBricksInRegion(int rx,int ry,int rw,int rh) {
  for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i) {
    Brick &b = bricks[i];
    if (!b.alive) continue;
    if (rectIntersect(rx, ry, rw, rh, b.x, b.y, b.w, b.h)) {
      const uint16_t base = currentBrickBaseColor(i);
      drawBrick3D_at(b.x, b.y, b.w, b.h, base, BRICK_HOVER_DELTA);
      if (brickCrackSeed[i]) drawCrackOverlayRandom(b.x, b.y, b.w, b.h, brickCrackSeed[i]);
    }
  }
}

// ---------- Particelle / Esplosioni ----------
static inline void resetParticles() { for (int i=0;i<MAX_PARTICLES;++i) particles[i].alive = false; }
static inline void flashRect(int x,int y,int w,int h) {
  gfx->fillRect(x, y, w, h, 0xFFFF);
  delay(16);
  safeClearRegion(x, y, w, h);
}
static void spawnExplosion(int cx,int cy,int w,int h, uint16_t color, int count = 8, bool oobOnly = false, bool behindPaddle = false) {
  for (int n=0;n<count;++n) {
    int idx = -1;
    for (int i=0;i<MAX_PARTICLES;++i) { if (!particles[i].alive) { idx=i; break; } }
    if (idx<0) break;
    Particle &p = particles[idx];
    p.alive = true;
    p.x = cx + w/2 + (random(-w/2, w/2));
    p.y = cy + h/2 + (random(-h/2, h/2));
    const float angle = (float)random(0, 628) / 100.0f;
    const float speed = 1.0f + (float)random(0, 200)/100.0f;
    p.vx = cosf(angle)*speed;
    p.vy = sinf(angle)*speed - 0.35f;
    p.oobOnly = oobOnly;
    p.behindPaddle = behindPaddle;
    p.life = oobOnly ? 220 : (uint8_t)((16 + random(0,6)) * 1.1f);

    const uint16_t core = color;
    const uint16_t dim  = (((core & 0xF800) >> 1) & 0xF800)
                        | (((core & 0x07E0) >> 1) & 0x07E0)
                        | (((core & 0x001F) >> 1) & 0x001F);
    p.color = (n % 3 == 0) ? core : dim;

    p.sz = (random(0,10) == 0) ? 3 : 2;
    p.px = (int)p.x; p.py = (int)p.y;
  }
  // frammento grosso
  int idx = -1;
  for (int i=0;i<MAX_PARTICLES;++i) { if (!particles[i].alive) { idx=i; break; } }
  if (idx>=0) {
    Particle &p = particles[idx];
    p.alive = true;
    p.x = cx + (w/2) + random(-w/4, w/4);
    p.y = cy + (h/2) + random(-h/4, h/4);
    const float angle = (float)random(0, 628) / 100.0f;
    const float speed = 1.2f + (float)random(0, 120)/100.0f;
    p.vx = cosf(angle)*speed;
    p.vy = sinf(angle)*speed - 0.3f;
    p.oobOnly = oobOnly;
    p.behindPaddle = behindPaddle;
    p.life = oobOnly ? 220 : (uint8_t)(22);
    p.color = color;
    p.sz = 4;
    p.px = (int)p.x; p.py = (int)p.y;
  }
}
static void updateParticles() {
  for (int i=0;i<MAX_PARTICLES;++i) {
    if (!particles[i].alive) continue;
    safeClearRegion(particles[i].px, particles[i].py, particles[i].sz, particles[i].sz);
  }
  for (int i=0;i<MAX_PARTICLES;++i) {
    Particle &p = particles[i];
    if (!p.alive) continue;

    p.vy += 0.08f;
    p.x  += p.vx;
    p.y  += p.vy;

    if (p.x<0 || p.x>=SCREEN_W || p.y<0 || p.y>=SCREEN_H) { p.alive=false; continue; }
    if (!p.oobOnly) { if (p.life>0) p.life--; else { p.alive=false; continue; } }

    const int ix = (int)p.x, iy = (int)p.y;
    gfx->fillRect(ix, iy, p.sz, p.sz, p.color);
    redrawBricksInRegion(ix, iy, p.sz, p.sz);
    p.px = ix; p.py = iy;
  }
}

// ---------- HUD ----------
static inline void drawSmallShipIcon(int x, int y) {
  const int w = BALL_SIZE;
  const int h = BALL_SIZE + 2;
  const int cx = x + (w>>1);
  gfx->fillTriangle(cx, y, x, y + h/2, x + w, y + h/2, COL_BALL);
  gfx->fillTriangle(cx, y + h/2, x + w/4, y + h, x + (3*w)/4, y + h, COL_GRAYL);
  gfx->fillTriangle(x, y + h/2, x + w/5, y + h, cx, y + h - 2, 0xF800);
  gfx->fillTriangle(x + w, y + h/2, x + w - w/5, y + h, cx, y + h - 2, 0xF800);
}
static void drawHUD_ifDirty() {
  const uint32_t now = millis();
  const bool dirty = (score != lastScore) || (lives != lastLives) || (now - lastHUDms >= 1000);
  if (!dirty) return;

  safeClearRegion(0, 0, SCREEN_W, 26);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(2);
  gfx->setCursor(10, 16);
  gfx->print(score);

  const int pad = 8;
  const int iconW = BALL_SIZE;
  const int totalW = 3*iconW + 2*pad;
  const int baseX = SCREEN_W - 10 - totalW;
  const int baseY = 2;
  safeClearRegion(baseX-2, 0, totalW+6, 26);
  for (int i=0;i<3;i++) {
    const int x = baseX + i*(iconW + pad);
    if (i < lives) drawSmallShipIcon(x, baseY);
  }

  lastScore = score;
  lastLives = lives;
  lastHUDms = now;
}

// ---------- Testi centrati ----------
static inline void drawCenteredMessageSized(const char *msg, uint8_t size) {
  const int len = strlen(msg);
  const int w = len * 6 * size;
  const int h = 8 * size;
  const int x = (SCREEN_W - w) / 2;
  const int y = (SCREEN_H - h) / 2;
  safeClearRegion(x - 10, y - 10, w + 20, h + 20);
  gfx->setTextSize(size);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(x, y + h);
  gfx->print(msg);
}
static inline void clearCenteredMessageSized(const char *msg, uint8_t size) {
  const int len = strlen(msg);
  const int w = len * 6 * size;
  const int h = 8 * size;
  const int x = (SCREEN_W - w) / 2;
  const int y = (SCREEN_H - h) / 2;
  safeClearRegion(x - 20, y - 20, w + 40, h + 40);
}
static inline void clearAnyShuffleMessage() {
  const int cw = 300, ch = 120;
  const int cx = (SCREEN_W - cw) / 2;
  const int cy = (SCREEN_H - ch) / 2;
  safeClearRegion(cx, cy, cw, ch, true);
  shuffleMsgUntilMs = 0;
}

// ---------- SCHERMATA GAME OVER (hard, no flicker) ----------
static inline void drawGameOverHard() {
  gfx->fillScreen(COL_BG);
  const char *msg = "GAME OVER";
  const uint8_t size = 6; // grande
  const int len = strlen(msg);
  const int w = len * 6 * size;
  const int h = 8 * size;
  const int x = (SCREEN_W - w) / 2;
  const int y = (SCREEN_H - h) / 2;
  gfx->setTextSize(size);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(x, y + h);
  gfx->print(msg);
}

// ---------- Gemma / power-up + power-down ----------
static inline void drawPowerUpDiamond(int x,int y,int w,int h, uint16_t core, uint16_t edge) {
  const int cx = x + (w>>1);
  const int cy = y + (h>>1);
  gfx->fillTriangle(cx, y,     x, cy,   x+w, cy, core);
  gfx->fillTriangle(cx, y+h,   x, cy,   x+w, cy, core);
  gfx->drawLine(x, cy, cx, y, edge);
  gfx->drawLine(cx, y, x+w, cy, edge);
  gfx->drawLine(x, cy, cx, y+h, edge);
  gfx->drawLine(cx, y+h, x+w, cy, edge);
}
static inline void drawBlinkingBonusLike(int x,int y,int w,int h, uint16_t coreBase) {
  const uint32_t now = millis();
  if (now - bonusBlinkLastMs >= BONUS_BLINK_MS) {
    bonusBlinkToggle = !bonusBlinkToggle;
    bonusBlinkLastMs = now;
  }
  const uint16_t core = bonusBlinkToggle ? COL_YELLOW : coreBase;
  const uint16_t edge = COL_PURPLE;
  drawPowerUpDiamond(x,y,w,h, core, edge);
}
static inline int randXForDrop() { return random(0, SCREEN_W - 18); }

// Spawn: mai power-up e power-down insieme; mai con missile presente
static inline bool canSpawnPickup() { return !anyPickupActive() && missilesActiveCount()==0; }

static void spawnBonus() {
  if (!canSpawnPickup()) { lastBonusMs = millis(); return; }
  bonusDrop.active = true;
  int tries = 8;
  int x = randXForDrop();
  while (tries-- && abs(x - pdownDrop.x) < DROPS_MIN_X_SEP) x = randXForDrop();
  bonusDrop.x = x;
  bonusDrop.y = 0;
  bonusDrop.px = bonusDrop.x; bonusDrop.py = bonusDrop.y;
  bonusDrop.vy = 3 + (random(0,2));
  bonusDrop.color = COL_CYAN;
}
static void spawnPowerDown() {
  if (!canSpawnPickup()) { lastPowerDownMs = millis(); return; }
  pdownDrop.active = true;
  int tries = 8;
  int x = randXForDrop();
  while (tries-- && abs(x - bonusDrop.x) < DROPS_MIN_X_SEP) x = randXForDrop();
  pdownDrop.x = x;
  pdownDrop.y = 0;
  pdownDrop.px = pdownDrop.x; pdownDrop.py = pdownDrop.y;
  pdownDrop.vy = 3 + (random(0,2));
  pdownDrop.color = COL_MAG;
}
static inline void clearBonusTrailStrip(int x, int y0, int y1, int w, int h) {
  int top = min(y0, y1) - 4;
  int bot = max(y0, y1) + h + 6;
  if (top < 0) top = 0; if (bot > SCREEN_H) bot = SCREEN_H;
  const int hh = bot - top; if (hh > 0) safeClearRegion(x-2, top, w+4, hh, true);
}
static inline void clearBonusLast()     { safeClearRegion(bonusDrop.px,  bonusDrop.py,  bonusDrop.w,  bonusDrop.h, true); }
static inline void clearPowerDownLast() { safeClearRegion(pdownDrop.px, pdownDrop.py, pdownDrop.w, pdownDrop.h, true); }
static inline void drawBonusNow() {
  if (!bonusDrop.active) return;
  drawBlinkingBonusLike(bonusDrop.x, bonusDrop.y, bonusDrop.w, bonusDrop.h, bonusDrop.color);
  redrawBricksInRegion(bonusDrop.x, bonusDrop.y, bonusDrop.w, bonusDrop.h);
}
static inline void drawPowerDownNow() {
  if (!pdownDrop.active) return;
  drawBlinkingBonusLike(pdownDrop.x, pdownDrop.y, pdownDrop.w, pdownDrop.h, pdownDrop.color);
  redrawBricksInRegion(pdownDrop.x, pdownDrop.y, pdownDrop.w, pdownDrop.h);
}
static inline void deactivateBonus()     { safeClearRegion(bonusDrop.x,  bonusDrop.y,  bonusDrop.w,  bonusDrop.h);  bonusDrop.active  = false; }
static inline void deactivatePowerDown() { safeClearRegion(pdownDrop.x, pdownDrop.y, pdownDrop.w, pdownDrop.h);    pdownDrop.active  = false; }

// ---------- Paddle FX lampeggio ----------
static inline void maybeUpdatePaddleBlinkFx() {
  if (!paddleBlinkFxActive) return;
  const uint32_t now = millis();
  if ((int32_t)(now - paddleBlinkEndMs) >= 0) {
    COL_PADDLE = COL_PADDLE_DEF;
    COL_PEND   = COL_PEND_DEF;
    paddleBlinkFxActive = false;
    clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
    drawPaddleRounded(paddle);
    return;
  }
  if (now - paddleBlinkLastMs >= PADDLE_BLINK_MS) {
    paddleBlinkLastMs = now;
    paddleBlinkStep = (paddleBlinkStep + 1) & 0x07;
    switch (paddleBlinkStep) {
      case 0: COL_PADDLE = COL_CYAN;   COL_PEND = COL_MAG;    break;
      case 1: COL_PADDLE = COL_YELLOW; COL_PEND = COL_ORANGE; break;
      case 2: COL_PADDLE = 0x07E0;     COL_PEND = 0x001F;     break;
      case 3: COL_PADDLE = 0xFFFF;     COL_PEND = 0xF800;     break;
      case 4: COL_PADDLE = COL_ORANGE; COL_PEND = COL_PURPLE; break;
      case 5: COL_PADDLE = COL_MAG;    COL_PEND = COL_YELLOW; break;
      case 6: COL_PADDLE = 0x001F;     COL_PEND = 0xFFE0;     break;
      default:COL_PADDLE = COL_GRAYL;  COL_PEND = COL_CYAN;    break;
    }
    clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
    drawPaddleRounded(paddle);
  }
}

// --- Applicazione buff/debuff e clamp ---
static inline void clampPaddleInside() {
  if (paddle.x < 0) paddle.x = 0;
  if (paddle.x + paddle.w > SCREEN_W) paddle.x = SCREEN_W - paddle.w;
}
static inline void applyPaddleBuff() {
  if (paddleBuffActive && paddleSizeState == PS_BUFF) return;
  const int cx = paddle.x + (paddle.w>>1);
  const int newW = PADDLE_W_BUFF;
  clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
  paddle.x = cx - (newW>>1);
  paddle.w = newW;
  clampPaddleInside();
  paddleBuffActive = true;
  paddleSizeState  = PS_BUFF;
  paddleBuffEndMs = millis() + PADDLE_BUFF_MS;
  paddleBlinkFxActive = true;
  paddleBlinkEndMs    = millis() + PADDLE_BLINK_DUR;
  paddleBlinkLastMs   = 0;
  paddleBlinkStep     = 0;
  drawPaddleRounded(paddle);
}
static inline void applyPaddleDebuff() {
  if (paddleBuffActive && paddleSizeState == PS_DEBUFF) return;
  const int cx = paddle.x + (paddle.w>>1);
  const int newW = PADDLE_W_DEBUFF;
  clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
  paddle.x = cx - (newW>>1);
  paddle.w = newW;
  clampPaddleInside();
  paddleBuffActive = true;
  paddleSizeState  = PS_DEBUFF;
  paddleBuffEndMs = millis() + PADDLE_BUFF_MS;
  paddleBlinkFxActive = true;
  paddleBlinkEndMs    = millis() + PADDLE_BLINK_DUR;
  paddleBlinkLastMs   = 0;
  paddleBlinkStep     = 0;
  drawPaddleRounded(paddle);
}
static inline void maybeExpirePaddleBuff() {
  if (!paddleBuffActive) return;
  if ((int32_t)(millis() - paddleBuffEndMs) >= 0) {
    clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
    const int cx = paddle.x + (paddle.w>>1);
    paddle.w = PADDLE_W_BASE;
    paddle.x = cx - (paddle.w>>1);
    clampPaddleInside();
    drawPaddleRounded(paddle);
    paddleBuffActive = false;
    paddleSizeState  = PS_NORMAL;
  }
}
static inline void normalizePaddleWidth() {
  const int targetW =
    (!paddleBuffActive || paddleSizeState == PS_NORMAL) ? PADDLE_W_BASE :
    (paddleSizeState == PS_BUFF) ? PADDLE_W_BUFF : PADDLE_W_DEBUFF;

  if (paddle.w != targetW) {
    clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
    const int cx = paddle.x + (paddle.w>>1);
    paddle.w = targetW;
    paddle.x = cx - (paddle.w>>1);
    clampPaddleInside();
    drawPaddleRounded(paddle);
  }
}

// ---------- Missili ----------
static inline void drawMissileStylized(int x,int y,int w,int h, uint16_t nose, uint16_t body, uint16_t fin, uint16_t flame1, uint16_t flame2) {
  const int cx = x + (w>>1);
  const int neckY = y + (h * 4) / 10;
  const int finY  = y + (h * 7) / 10;
  const int flick = random(0,3);
  gfx->fillTriangle(cx, y, x, neckY, x+w, neckY, nose);
  const int mid1 = (neckY + finY) / 2;
  gfx->fillTriangle(cx, neckY, x + w/4, mid1, x + (3*w)/4, mid1, body);
  gfx->fillTriangle(cx, mid1, x + w/3, finY, x + (2*w)/3, finY, body);
  gfx->fillTriangle(x, finY, x + w/4, finY, cx, finY + h/10, fin);
  gfx->fillTriangle(x+w, finY, x + (3*w)/4, finY, cx, finY + h/10, fin);
  gfx->fillTriangle(cx, y+h + flick, cx - w/6, y+h - h/12, cx + w/6, y+h - h/12, flame1);
  if (random(0,2)) gfx->fillTriangle(cx, y+h + flick + 2, cx - w/10, y+h - h/20, cx + w/10, y+h - h/20, flame2);
}
static inline void resetMissiles() { for (int i=0;i<MAX_MISSILES;++i) missiles[i].active = false; }
static void spawnMissile(int sx, int sy) {
  // vincoli richiesti: non se c'è un pickup attivo, e max 1 missile a schermo
  if (anyPickupActive()) return;
  if (missilesActiveCount() >= 1) return;
  for (int i=0;i<MAX_MISSILES;++i) if (!missiles[i].active) {
    missiles[i].active = true;
    missiles[i].w = 10; missiles[i].h = 18;
    missiles[i].x = sx + (BRICK_W - missiles[i].w)/2;
    missiles[i].y = sy + BRICK_H;
    missiles[i].px = missiles[i].x; missiles[i].py = missiles[i].y;
    missiles[i].vy = 4; missiles[i].color = 0xFBE0;
    return;
  }
}
static inline void clearMissileTrailStrip(int x, int y0, int y1, int w, int h) {
  int top = min(y0, y1) - 4;
  int bot = max(y0, y1) + h + 6;
  if (top < 0) top = 0; if (bot > SCREEN_H) bot = SCREEN_H;
  const int hh = bot - top; if (hh > 0) safeClearRegion(x-2, top, w+4, hh, true);
}
static inline void drawMissileNow(int i) {
  const int x = missiles[i].x, y = missiles[i].y, w = missiles[i].w, h = missiles[i].h;
  drawMissileStylized(x,y,w,h, missiles[i].color, COL_GRAYL, 0xF800, COL_YELLOW, COL_ORANGE);
  redrawBricksInRegion(x, y, w, h + 6);
}
static inline void cleanupParticlesInRect(int x,int y,int w,int h) {
  for (int i=0;i<MAX_PARTICLES;++i) {
    Particle &p = particles[i];
    if (!p.alive) continue;
    const int ix = (int)p.x, iy = (int)p.y;
    if (ix >= x && ix < x+w && iy >= y && iy < y+h) {
      safeClearRegion(p.px, p.py, p.sz, p.sz);
      p.alive = false;
    }
  }
}
static inline void animateMissileExplosion(int x,int y,int w,int h,uint16_t color) {
  flashRect(x-1, y-1, w+2, h+2);
  spawnExplosion(x, y, w, h, color, 8, false, false);
  const uint32_t t0 = millis();
  while (millis() - t0 < 220) { updateParticles(); delay(12); }
  const int cx = x-8, cy = y-8, cw = w+16, ch = h+16;
  safeClearRegion(cx, cy, cw, ch);
  cleanupParticlesInRect(cx, cy, cw, ch);
}

// ---------- Frecce guida (solo icone, stessa riga, dentro schermo) ----------
static inline void drawCornerHints() {
  const int m = 14;     // margine laterale
  const int h = 12;     // altezza freccia
  const int w = 16;     // larghezza freccia
  const int ay = SCREEN_H - m - h;  // baseline comune

  // sinistra
  gfx->fillTriangle(m+w, ay,    m+w, ay+h,    m,    ay + h/2, COL_HINT);
  // destra
  gfx->fillTriangle(SCREEN_W - m - w, ay,
                    SCREEN_W - m - w, ay+h,
                    SCREEN_W - m,     ay + h/2, COL_HINT);
  // (TESTO RIMOSSO)
}

// ---------- Stato/Score/Palla ----------
static inline void resetBallAndPaddle(bool centerBall = true) {
  paddle.h = PADDLE_H;
  if (!paddleBuffActive || paddleSizeState == PS_NORMAL) {
    paddle.w = PADDLE_W_BASE;
  } else {
    paddle.w = (paddleSizeState == PS_BUFF) ? PADDLE_W_BUFF : PADDLE_W_DEBUFF;
  }
  paddle.x = (SCREEN_W - paddle.w) / 2;
  paddle.y = PADDLE_Y;
  if (centerBall) {
    ball.x = paddle.x + paddle.w/2 - BALL_SIZE/2;
    ball.y = paddle.y - BALL_SIZE - 2;
  }
  ball.vx = 0.0f; ball.vy = 0.0f; ballOnPaddle = true;
}

static inline void fullRedraw() {
  gfx->fillScreen(COL_BG);
  drawStars();
  drawBricks(true);
  drawPaddleRounded(paddle);
  drawBallNow();
  drawHUD_ifDirty();
  drawCornerHints();
}

static inline void showCenteredMessage(const char *msg) {
  drawCenteredMessageSized(msg, 2);
}

// ---------- Esplosione navicella ----------
static inline void animatePaddleExplosion() {
  clearPaddleRegionExpanded(paddle.x, paddle.y, paddle.w, paddle.h);
  flashRect(paddle.x-2, paddle.y-2, paddle.w+4, paddle.h+4);
  const int cx = paddle.x + (paddle.w>>1);
  const int cy = paddle.y + (paddle.h>>1);
  const int maxR = (paddle.w > 40 ? paddle.w : 40); // fix tipo
  for (int r = 6; r <= maxR; r += 6) {
    gfx->drawCircle(cx, cy, r, COL_YELLOW);
    gfx->drawCircle(cx, cy, r-2, COL_ORANGE);
    delay(8);
    safeClearRegion(cx - r - 3, cy - r - 3, (r + 3) * 2, (r + 3) * 2, true);
  }
  spawnExplosion(paddle.x, paddle.y, paddle.w, paddle.h, COL_PADDLE_DEF, 14, false, false);
  spawnExplosion(paddle.x, paddle.y, paddle.w, paddle.h, COL_PEND_DEF,   12, false, false);

  const uint32_t t0 = millis();
  while (millis() - t0 < 420) { updateParticles(); delay(12); }

  const int cxr = paddle.x - 16, cyr = paddle.y - 16, cwr = paddle.w + 32, chr = paddle.h + 32;
  safeClearRegion(cxr, cyr, cwr, chr);
  cleanupParticlesInRect(cxr, cyr, cwr, chr);
}

// --- Perdita vita ---
static inline void loseLifeAfterAnimation(int oldBX, int oldBY) {
  lives--;
  clearBallRegionExpanded(oldBX, oldBY);
  clearBallRegionExpanded((int)ball.x, (int)ball.y);
  clearPaddleSafeBand();

  // respawn sempre dimensione standard
  paddleBuffActive = false;
  paddleSizeState  = PS_NORMAL;

  if (lives <= 0) {
    gameOver = true;
    gameOverDrawn = false;
    clearAnyShuffleMessage();
    return;
  } else {
    resetBallAndPaddle(true);
    drawPaddleRounded(paddle);
    drawBallNow();
  }
}

// ---------- Input ----------
static inline Quad readQuadrant() {
  ts.read();
  if (ts.isTouched && ts.touches > 0) {
    const int x = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, 479);
    const int y = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, 479);
    const bool bottom = (y > SCREEN_H * 0.75f);
    if (bottom) {
      if (x < SCREEN_W * 0.25f)  return Q_BL;
      if (x > SCREEN_W * 0.75f)  return Q_BR;
    }
    return Q_TL;
  }
  return Q_NONE;
}

// ---------- Splash screen (6s) ----------
static inline void showSplash(uint32_t ms = 6000) {
  gfx->fillScreen(0x0000);
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)Pongino, PONGINO_WIDTH, PONGINO_HEIGHT);
  const uint32_t t0 = millis();
  while ((millis() - t0) < ms) delay(10);
  gfx->fillScreen(COL_BG);
}

// ---------- Shuffle dei mattoni ----------
static void suddenShuffle() {
  int aliveCount = 0;
  for (int i=0;i<BRICK_ROWS*BRICK_COLS;++i) if (bricks[i].alive) aliveCount++;

  const int TOT = BRICK_ROWS*BRICK_COLS;
  int indices[TOT];
  for (int i=0;i<TOT;++i) indices[i]=i;
  for (int i=TOT-1;i>0;--i){ int j = random(0, i+1); int t = indices[i]; indices[i]=indices[j]; indices[j]=t; }

  for (int i=0;i<TOT;++i) bricks[i].alive=false;

  const uint16_t pal[5] = {COL_BRICK1, COL_BRICK2, COL_BRICK3, COL_BRICK4, COL_BRICK5};
  const int set = min(aliveCount, TOT);
  for (int k=0;k<set;++k){
    const int idx = indices[k];
    const int r = idx / BRICK_COLS;
    const int c = idx % BRICK_COLS;
    Brick &b = bricks[idx];
    b.w = BRICK_W; b.h = BRICK_H;
    b.x = BRICK_PAD + c * (BRICK_W + BRICK_PAD);
    b.y = BRICK_TOP + r * (BRICK_H + BRICK_PAD);
    b.alive = true;
    const uint16_t col = pal[r % 5];
    b.color = col;
    brickOrigColor[idx] = col;
    brickPalIndex[idx]  = (uint8_t)(r % 5);
  }
  bricksAlive = aliveCount;

  drawBricks(true);
}

// ---------- Setup ----------
void setup() {
  delay(50);
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  delay(50);
  gfx->begin();
  gfx->setRotation(0);
  delay(50);

  showSplash(6000);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  ts.begin();

  randomSeed((analogRead(A0) ^ micros()));

  initStars();
  resetParticles();
  resetMissiles();
  initBricks();

  lives = 3; victory = false; gameOver = false; score = 0;
  gameOverDrawn = false;

  // Stato iniziale navicella: dimensione standard
  paddleBuffActive = false;
  paddleSizeState  = PS_NORMAL;
  paddleBuffEndMs  = 0;

  lastBonusMs = millis();
  lastPowerDownMs = millis();
  lastShuffleMs = millis();
  bonusDrop.active = false;
  pdownDrop.active = false;
  bonusBlinkToggle = false; bonusBlinkLastMs = 0;

  resetBallAndPaddle(true);
  fullRedraw();
}

// ---------- Gestione fasi palette (opzionale) ----------
static inline void stepPalettePhase(){
  if (palPhase == PAL_ORIG_FADE || palPhase == PAL_ALT_FADE) {
    if (palFadeCycles >= 2) {
      palXAlpha = 0;
      palPhase = (palPhase == PAL_ORIG_FADE) ? PAL_XFADE_TO_ALT : PAL_XFADE_TO_ORIG;
      palFadeCycles = 0;
    }
  } else {
    // velocità transizione ridotta del 30% (10 -> 7)
    if (palXAlpha < 255) palXAlpha = (uint8_t)min(255, (int)palXAlpha + 7);
    else {
      if (palPhase == PAL_XFADE_TO_ALT) {
        palAltActive = true;
        palPhase = PAL_ALT_FADE;
      } else {
        palAltActive = false;
        palPhase = PAL_ORIG_FADE;
      }
      palFadeCycles = 0;
      palXAlpha = 0;
    }
  }
}

// ---------- Loop ----------
void loop() {
  static uint32_t lastFrame = 0;
  const uint32_t now = millis();
  if (now - lastFrame < (uint32_t)FRAME_INTERVAL_MS) return;
  lastFrame = now;

  // INPUT prioritario per stati finali
  Quad q = readQuadrant();

  // === Stato GAME OVER ===
  if (gameOver) {
    if (!gameOverDrawn) {
      drawGameOverHard();
      gameOverDrawn = true;
    }
    if (q != Q_NONE) {
      // restart completo
      score = 0; lives = 3; victory = false; gameOver = false; gameOverDrawn = false;
      paddleBuffActive = false; paddleSizeState = PS_NORMAL; paddleBuffEndMs = 0;
      bonusDrop.active = false; pdownDrop.active = false;
      resetMissiles(); resetParticles(); initBricks();
      COL_PADDLE = COL_PADDLE_DEF; COL_PEND = COL_PEND_DEF;
      resetBallAndPaddle(true);
      fullRedraw();
    }
    return;
  }

  // Hover/scintillio + conteggio cicli
  if (now - BRICK_HOVER_LAST >= BRICK_HOVER_STEP_MS){
    BRICK_HOVER_LAST = now;
    BRICK_HOVER_DELTA += BRICK_HOVER_DIR;
    if (BRICK_HOVER_DELTA >= BRICK_HOVER_MAX) {
      BRICK_HOVER_DELTA = BRICK_HOVER_MAX;
      BRICK_HOVER_DIR = -1;
      hoverHitTop = true;
    }
    if (BRICK_HOVER_DELTA <= -BRICK_HOVER_MAX){
      BRICK_HOVER_DELTA = -BRICK_HOVER_MAX;
      BRICK_HOVER_DIR = +1;
      if (hoverHitTop) {
        palFadeCycles++;     // un ciclo completo
        hoverHitTop = false;
      }
    }
    stepPalettePhase();
    BRICK_NEED_REPAINT = true;
  }

  maybeExpirePaddleBuff();
  maybeUpdatePaddleBlinkFx();
  normalizePaddleWidth();

  // Shuffle periodico ogni 40s
  if ((uint32_t)(now - lastShuffleMs) >= 40000) {
    lastShuffleMs = now;
    suddenShuffle();
    shuffleMsgUntilMs = now + 2000; // 2s
  }

  // ======= SPAWN TEMPORIZZATI PICKUP =======
  if (!anyPickupActive() && missilesActiveCount()==0) {
    if ((uint32_t)(now - lastBonusMs) >= BONUS_PERIOD_MS) {
      spawnBonus();
      lastBonusMs = now;
    } else if ((uint32_t)(now - lastPowerDownMs) >= PDOWN_PERIOD_MS) {
      spawnPowerDown();
      lastPowerDownMs = now;
    }
  }

  if (victory) {
    if (BRICK_NEED_REPAINT) { drawBricks(false); BRICK_NEED_REPAINT = false; }
    showCenteredMessage("VICTORY! - tap to restart");
    drawHUD_ifDirty();
    drawCornerHints();
    if (q != Q_NONE) {
      score = 0; lives = 3; victory = false; gameOver = false; gameOverDrawn = false;
      paddleBuffActive = false; paddleSizeState = PS_NORMAL; paddleBuffEndMs = 0;
      bonusDrop.active = false; pdownDrop.active = false;
      resetMissiles(); resetParticles(); initBricks();
      COL_PADDLE = COL_PADDLE_DEF; COL_PEND = COL_PEND_DEF;
      resetBallAndPaddle(true);
      fullRedraw();
    }
    return;
  }

  const int oldPX = paddle.x, oldPY = paddle.y, oldPW = paddle.w;
  const int oldBX = (int)ball.x, oldBY = (int)ball.y;

  if (q == Q_BL) paddle.x -= PADDLE_SPEED;
  else if (q == Q_BR) paddle.x += PADDLE_SPEED;

  clampPaddleInside();

  if (ballOnPaddle) {
    if ((oldPX != paddle.x) || (oldPY != paddle.y) || (oldPW != paddle.w)) {
      ball.x = paddle.x + paddle.w/2 - BALL_SIZE/2;
      ball.y = paddle.y - BALL_SIZE - 2;
    }
    if (q == Q_BL)  { ball.vx = -BALL_SPEED_INIT; ball.vy = -BALL_SPEED_INIT; ballOnPaddle = false; }
    else if (q == Q_BR) { ball.vx =  BALL_SPEED_INIT; ball.vy = -BALL_SPEED_INIT; ballOnPaddle = false; }
  } else {
    ball.x += ball.vx; ball.y += ball.vy;

    if (ball.x <= 0) { ball.x = 0; ball.vx = fabs(ball.vx); }
    if (ball.x + BALL_SIZE >= SCREEN_W) { ball.x = SCREEN_W - BALL_SIZE; ball.vx = -fabs(ball.vx); }
    if (ball.y <= 0) { ball.y = 0; ball.vy = fabs(ball.vy); }

    if (rectIntersect((int)ball.x, (int)ball.y, BALL_SIZE, BALL_SIZE, paddle.x, paddle.y, paddle.w, paddle.h) && ball.vy > 0) {
      ball.y  = paddle.y - BALL_SIZE - 1;
      ball.vy = -fabs(ball.vy);
      const float hit = ((ball.x + BALL_SIZE/2) - (paddle.x + paddle.w/2)) / (float)(paddle.w/2);
      ball.vx += hit * 1.1f;
      const float spx = constrain(fabs(ball.vx), 1.2f, BALL_SPEED_MAX);
      ball.vx = (ball.vx < 0 ? -spx : spx);
      const float spy = constrain(fabs(ball.vy)+BALL_SPEED_INC, 1.6f, BALL_SPEED_MAX);
      ball.vy = (ball.vy < 0 ? -spy : spy);
    }

    // --- Collisione con mattoni (si rompe) ---
    bool brokeBrick = false;
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i) {
      Brick &b = bricks[i];
      if (!b.alive) continue;
      if (rectIntersect((int)ball.x, (int)ball.y, BALL_SIZE, BALL_SIZE, b.x, b.y, b.w, b.h)) {
        b.alive = false; bricksAlive--; score += 10;
        brickCrackSeed[i] = 0; brickHits[i] = 0;

        // crepe sui vicini
        const int r = i / BRICK_COLS, c = i % BRICK_COLS;
        auto crackNeighbor = [&](int rr, int cc){
          if (rr<0 || rr>=BRICK_ROWS || cc<0 || cc>=BRICK_COLS) return;
          const int ni = rr*BRICK_COLS + cc;
          if (!bricks[ni].alive) return;
          if (brickCrackSeed[ni] == 0) brickCrackSeed[ni] = (uint32_t)random(1, 0x7FFFFFFF);
          drawCrackOverlayRandom(bricks[ni].x, bricks[ni].y, bricks[ni].w, bricks[ni].h, brickCrackSeed[ni]);
        };
        crackNeighbor(r, c-1); crackNeighbor(r, c+1); crackNeighbor(r-1, c); crackNeighbor(r+1, c);

        flashRect(b.x, b.y, b.w, b.h);
        spawnExplosion(b.x, b.y, b.w, b.h, b.color, 6, true, true);
        if (brickHasMissile[i]) { spawnMissile(b.x, b.y); brickHasMissile[i] = false; }

        const int bxC = (int)ball.x + (BALL_SIZE>>1);
        const int byC = (int)ball.y + (BALL_SIZE>>1);
        const int bcx = b.x + (b.w>>1);
        const int bcy = b.y + (b.h>>1);
        const int dx = bxC - bcx;
        const int dy = byC - bcy;
        if (abs(dx) > abs(dy)) ball.vx = (dx > 0) ?  fabs(ball.vx) : -fabs(ball.vx);
        else                   ball.vy = (dy > 0) ?  fabs(ball.vy) : -fabs(ball.vy);

        brokeBrick = true;
        break;
      }
    }

    // --- Colpito ma NON si rompe subito -> si crepa sempre ---
    if (!brokeBrick) {
      const int INF = 2;
      for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i) {
        Brick &b = bricks[i];
        if (!b.alive) continue;
        if (rectIntersectInflated((int)ball.x, (int)ball.y, BALL_SIZE, BALL_SIZE, b.x, b.y, b.w, b.h, INF)) {
          const int bxC = (int)ball.x + (BALL_SIZE>>1);
          const int byC = (int)ball.y + (BALL_SIZE>>1);
          const int bcx = b.x + (b.w>>1);
          const int bcy = b.y + (b.h>>1);
          const int dx = bxC - bcx;
          const int dy = byC - bcy;
          if (abs(dx) > abs(dy)) ball.vx = (dx > 0) ?  fabs(ball.vx) : -fabs(ball.vx);
          else                   ball.vy = (dy > 0) ?  fabs(ball.vy) : -fabs(ball.vy);

          brickHits[i] = (uint8_t)min(255, (int)brickHits[i] + 1);

          if (brickCrackSeed[i] == 0) brickCrackSeed[i] = (uint32_t)random(1, 0x7FFFFFFF);
          drawCrackOverlayRandom(b.x, b.y, b.w, b.h, brickCrackSeed[i]);

          if (brickHits[i] >= 2) {
            b.alive = false; bricksAlive--; score += 10;
            brickCrackSeed[i] = 0; brickHits[i] = 0;

            const int r = i / BRICK_COLS, c = i % BRICK_COLS;
            auto crackNeighbor2 = [&](int rr, int cc){
              if (rr<0 || rr>=BRICK_ROWS || cc<0 || cc>=BRICK_COLS) return;
              const int ni = rr*BRICK_COLS + cc;
              if (!bricks[ni].alive) return;
              if (brickCrackSeed[ni] == 0) brickCrackSeed[ni] = (uint32_t)random(1, 0x7FFFFFFF);
              drawCrackOverlayRandom(bricks[ni].x, bricks[ni].y, bricks[ni].w, bricks[ni].h, brickCrackSeed[ni]);
            };
            crackNeighbor2(r, c-1); crackNeighbor2(r, c+1); crackNeighbor2(r-1, c); crackNeighbor2(r+1, c);

            flashRect(b.x, b.y, b.w, b.h);
            spawnExplosion(b.x, b.y, b.w, b.h, b.color, 6, true, true);
            if (brickHasMissile[i]) { spawnMissile(b.x, b.y); brickHasMissile[i] = false; }
          }
          break;
        }
      }
    }

    if (ball.y > SCREEN_H) {
      animatePaddleExplosion();
      loseLifeAfterAnimation(oldBX, oldBY);
      return;
    }

    if (bricksAlive == 0) {
      victory = true;
      clearAnyShuffleMessage();
      showCenteredMessage("VICTORY! - tap to restart");
      drawHUD_ifDirty(); drawCornerHints();
      return;
    }
  }

  // Power-up
  if (bonusDrop.active) {
    const int prevY = bonusDrop.y, prevX = bonusDrop.x;
    bonusDrop.py = bonusDrop.y; bonusDrop.px = bonusDrop.x;
    bonusDrop.y += bonusDrop.vy;

    clearBonusTrailStrip(prevX, prevY, bonusDrop.y, bonusDrop.w, bonusDrop.h);

    if (bonusDrop.y > SCREEN_H) {
      deactivateBonus();
    } else {
      if (rectIntersect(bonusDrop.x, bonusDrop.y, bonusDrop.w, bonusDrop.h, paddle.x, paddle.y, paddle.w, paddle.h)) {
        deactivateBonus();
        applyPaddleBuff();
      } else {
        drawBonusNow();
      }
    }
  }

  // Power-down
  if (pdownDrop.active) {
    const int prevY = pdownDrop.y, prevX = pdownDrop.x;
    pdownDrop.py = pdownDrop.y; pdownDrop.px = pdownDrop.x;
    pdownDrop.y += pdownDrop.vy;

    clearBonusTrailStrip(prevX, prevY, pdownDrop.y, pdownDrop.w, pdownDrop.h);

    if (pdownDrop.y > SCREEN_H) {
      deactivatePowerDown();
    } else {
      if (rectIntersect(pdownDrop.x, pdownDrop.y, pdownDrop.w, pdownDrop.h, paddle.x, paddle.y, paddle.w, paddle.h)) {
        deactivatePowerDown();
        applyPaddleDebuff();
      } else {
        drawPowerDownNow();
      }
    }
  }

  // Missili
  for (int i=0;i<MAX_MISSILES;++i) {
    if (!missiles[i].active) continue;
    const int prevY = missiles[i].y, prevX = missiles[i].x;
    missiles[i].y += missiles[i].vy;
    clearMissileTrailStrip(prevX, prevY, missiles[i].y, missiles[i].w, missiles[i].h);

    if (rectIntersect(missiles[i].x, missiles[i].y, missiles[i].w, missiles[i].h, paddle.x, paddle.y, paddle.w, paddle.h)) {
      animateMissileExplosion(missiles[i].x, missiles[i].y, missiles[i].w, missiles[i].h, missiles[i].color);
      missiles[i].active = false;
      animatePaddleExplosion();
      loseLifeAfterAnimation(oldBX, oldBY);
      return;
    }
    if (missiles[i].y > SCREEN_H) { missiles[i].active = false; continue; }

    missiles[i].px = missiles[i].x; missiles[i].py = missiles[i].y;
    drawMissileNow(i);
  }

  // Repaint hover/palette mattoni
  if (BRICK_NEED_REPAINT) {
    drawBricks(false);
    BRICK_NEED_REPAINT = false;
  }

  // Messaggio "SUDDEN SHUFFLE" 2s + pulizia
  static bool shuffleMsgShown = false;
  if (shuffleMsgUntilMs && now < shuffleMsgUntilMs) {
    drawCenteredMessageSized("SUDDEN SHUFFLE", 2);
    shuffleMsgShown = true;
  } else if (shuffleMsgUntilMs && now >= shuffleMsgUntilMs) {
    if (shuffleMsgShown) {
      clearCenteredMessageSized("SUDDEN SHUFFLE", 2);
      clearAnyShuffleMessage();
      shuffleMsgShown = false;
    }
    shuffleMsgUntilMs = 0;
  }

  // Layering finale
  updateParticles();
  if (oldPX != paddle.x || oldPY != paddle.y || oldPW != paddle.w) {
    clearPaddleRegionExpanded(oldPX, oldPY, oldPW, PADDLE_H);
    drawPaddleRounded(paddle);
  }
  if (oldBX != (int)ball.x || oldBY != (int)ball.y) {
    clearBallRegionExpanded(oldBX, oldBY);
    drawBallNow();
  }
  drawHUD_ifDirty();
  drawCornerHints();
}
