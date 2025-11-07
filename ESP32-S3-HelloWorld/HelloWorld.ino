#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// --- PIN I2C per eventuale touch controller (non usati in questo esempio) ---
#define I2C_SDA_PIN 19
#define I2C_SCL_PIN 45

// --- CONTROLLO RETROILLUMINAZIONE tramite PWM (LEDC) ---
#define GFX_BL 38          // Pin collegato al backlight
#define PWM_CHANNEL 0      // Canale PWM utilizzato
#define PWM_FREQ    1000   // Frequenza del segnale PWM (1 kHz)
#define PWM_BITS    8      // Risoluzione del PWM a 8 bit (0–255)

// --- BUS COMANDI ST7701: utilizza interfaccia software SPI (SWSPI) ---
// Serve per inviare i comandi di inizializzazione al driver ST7701
Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC non utilizzato */,
    39 /* CS: chip select */,
    48 /* SCK: clock SPI software */,
    47 /* MOSI: dati SPI software */,
    GFX_NOT_DEFINED /* MISO non utilizzato */
);

// --- BUS PIXEL RGB: trasporta i dati grafici verso il pannello ---
// I segnali DE, VSYNC, HSYNC e PCLK gestiscono la sincronizzazione video
// I pin R, G, B inviano i valori dei singoli canali di colore
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    /* DE, VSYNC, HSYNC, PCLK */ 18, 17, 16, 21,
    /* R0..R4 */ 11, 12, 13, 14, 0,
    /* G0..G5 */ 8, 20, 3, 46, 9, 10,
    /* B0..B4 */ 4, 5, 6, 7, 15,
    /* hsync_pol, hfp, hpw, hbp */ 1, 10, 8, 50,   // parametri orizzontali
    /* vsync_pol, vfp, vpw, vbp */ 1, 10, 8, 20,   // parametri verticali
    /* pclk_active_neg */ 0,                       // polarità PCLK
    /* prefer_speed */ 12000000,                   // frequenza pixel clock
    /* big_endian */ false,                        // ordine byte standard
    /* de_idle_high, pclk_idle_high, bounce_buf */ 0, 0, 0
);

// --- OGGETTO DISPLAY principale ---
// Collega il bus comandi (SWSPI) e il bus dati (RGB)
// Usa la sequenza di inizializzazione predefinita st7701_type9_init_operations
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    480, 480,                   // Risoluzione del pannello
    rgbpanel,                   // Interfaccia RGB
    0 /* rotation */,           // Nessuna rotazione
    true /* auto_flush */,      // Aggiornamento automatico del frame buffer
    bus,                        // Bus comandi
    GFX_NOT_DEFINED /* RST */,  // Nessun pin di reset dedicato
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

void setup() {
    // --- Inizializzazione del PWM per la retroilluminazione ---
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
    ledcAttachPin(GFX_BL, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 255); // Luminosità massima

    // --- Avvio del bus I2C (solo se necessario per il touch) ---
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // --- Inizializzazione del display (driver + sincronizzazione RGB) ---
    gfx->begin();

    // --- Disegno di una semplice schermata di testo ---
    gfx->fillScreen(BLACK);                // Sfondo nero
    gfx->setTextColor(WHITE, BLACK);       // Testo bianco su sfondo nero
    gfx->setTextSize(2);                   // Dimensione testo media
    gfx->setCursor(120, 224);              // Posizionamento testo al centro
    gfx->print("Hello World");             // Scrittura messaggio
}

void loop() {
    // Ciclo principale vuoto: nessuna azione ripetuta
}
