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

Sì, i link sono veri.
Sì, puoi scannerizzarli e il browser ti porterà davvero su YouTube.
No, non troverai quasi mai un video.
(Statisticamente, è più facile vincere alla lotteria che beccare un ID esistente.)
Ma in fondo, è proprio questa l’essenza del progetto:
celebrare l’inutilità perfettamente funzionante.

## Licenza

Rilasciato sotto licenza Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
Puoi usarlo, modificarlo e condividerlo liberamente, purché non a scopo commerciale e citando l’autore.

"Il QR perfetto non apre nulla, ma apre la mente."
