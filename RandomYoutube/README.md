# Random YouTube QR · ESP32-S3 + 480x480 ST7701 Panel

Un piccolo esperimento grafico per ESP32-S3: genera, mostra e rigenera codici QR casuali che puntano a link perfettamente validi di YouTube... ma quasi mai a un video reale.

## Cos'è

All’avvio, l’app crea un link YouTube completo nel formato canonico:

```
https://www.youtube.com/watch?v=XXXXXXXXXXX
```

dove `XXXXXXXXXXX` è un ID di 11 caratteri alfanumerici puri (solo a fini pseudo-statistici), generati casualmente.
Per tre secondi viene mostrato l’URL sullo schermo, poi compare il QR code a pieno schermo.

Toccando lo schermo, ne viene generato uno nuovo.
Tutti i link sono formalmente validi per YouTube… solo che quasi nessuno porta a un video esistente.
È come pescare con un retino nell’oceano dei bit sperando di trovare un Rickroll... Chissà che non ve ne capiti uno ;)

## Hardware supportato

* ESP32-S3 board
* Display RGB 480×480 con driver ST7701 (es. Guition ESP32-4848S040)
* Touch capacitivo TAMC_GT911
* Libreria grafica: [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX)
* Libreria QR: [QRCodeGenerator di Felix Erdmann](https://github.com/bitbank2/QRCodeGenerator)
* Libreria touch: [TAMC_GT911](https://github.com/TAMC-Technology/TAMC_GT911)

## Funzionamento

1. Avvio → genera link YouTube casuale.
2. Mostra l’URL per 3 secondi.
3. Disegna il codice QR centrato su tutto lo schermo.
4. Ogni tocco → nuovo URL, nuovo QR, nuova (mancata) scoperta.

## Dettagli tecnici

* ID generato con caratteri `A–Z a–z 0–9`, nessun trattino né underscore.
* Algoritmo garantisce almeno una maiuscola, una minuscola e una cifra.
* QR versione 5 (37×37 moduli) con correzione d’errore media.
* Disegno pixel-perfect tramite Arduino_GFX.
* Touch gestito con GT911 via I²C.

## Nota ironica

Sì, i link sono veri. No, non troverai *quasi* mai un video.

## Licenza

Rilasciato sotto licenza Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
Puoi usarlo, modificarlo e condividerlo liberamente, purché non a scopo commerciale e citando l’autore.

---

## English Version

# Random YouTube QR · ESP32-S3 + 480x480 ST7701 Panel

A small graphics experiment for the ESP32-S3: it generates, displays, and regenerates random QR codes that point to fully valid
YouTube links… which almost never lead to a real video.

## What it is

At startup, the app creates a complete YouTube link in the canonical format:

```
https://www.youtube.com/watch?v=XXXXXXXXXXX
```

where `XXXXXXXXXXX` is an 11-character alphanumeric ID (purely for pseudo-statistical purposes), generated at random.
The URL is shown on screen for three seconds, then a full-screen QR code appears.

Touching the screen generates a new one.
All links are formally valid for YouTube… they just rarely point to an existing video.
It’s like fishing with a net in the ocean of bits hoping to catch a Rickroll… maybe you’ll get lucky ;)

## Supported hardware

* ESP32-S3 board
* 480×480 RGB display with ST7701 driver (e.g. Guition ESP32-4848S040)
* TAMC_GT911 capacitive touch
* Graphics library: [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX)
* QR library: [QRCodeGenerator by Felix Erdmann](https://github.com/bitbank2/QRCodeGenerator)
* Touch library: [TAMC_GT911](https://github.com/TAMC-Technology/TAMC_GT911)

## How it works

1. Start → generate a random YouTube link.
2. Display the URL for 3 seconds.
3. Draw the centred QR code across the entire screen.
4. Each touch → new URL, new QR, another (missed) discovery.

## Technical details

* ID generated using characters `A–Z a–z 0–9`, no hyphen or underscore.
* Algorithm ensures at least one uppercase, one lowercase, and one digit.
* QR version 5 (37×37 modules) with medium error correction.
* Pixel-perfect rendering via Arduino_GFX.
* Touch handled with GT911 over I²C.

## Ironic note

Yes, the links are real. No, you will *almost* never find a video.

## License

Released under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) license.
You may use, modify, and share it freely, provided it is not for commercial purposes and the author is credited.
