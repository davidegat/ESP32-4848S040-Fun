# ESP32-S3 Panel-4848S040 · Pixel Art Grid + Palette + Clear/Export

> **Autore:** Davide Nasato (gat)
> **Repo:** [https://github.com/davidegat/ESP32-4848S040-Fun](https://github.com/davidegat/ESP32-4848S040-Fun)
> **Nota:** software realizzato anche con l’aiuto di modelli linguistici (LLM)

## Panoramica

Questo sketch trasforma il pannello **ESP32-S3 Panel-4848S040** (display 480×480 con controller **ST7701** e touch **GT911**) in una semplice app di **pixel art**:

* **Griglia a sinistra (400×480)**: 20 colonne × 24 righe, celle 20×20 px.
* **Palette a destra (80 px)**: 5 colori selezionabili.
* **Barra comandi in basso (centrata)**:

  * **CLEAR**: pulisce l’intera griglia.
  * **EXPORT**: salva l’area griglia (400×480) su **microSD** in **JPEG** (se presente la libreria *JPEGENC*), altrimenti in **BMP** (fallback).

Include il **toggle colore**: toccando una cella già colorata, la cella torna bianca e può essere poi colorata nuovamente.
Il tocco è gestito con **edge detection** + **cooldown** per evitare doppi tap o attivazioni multiple.

---

## Hardware supportato

* Scheda: **ESP32-S3 Panel-4848S040** (vedi [scheda tecnica HomeDing](https://homeding.github.io/boards/esp32s3/panel-4848S040.htm)).
* Display: **ST7701** (RGB + init via SWSPI).
* Touch: **GT911** su I²C.
* microSD: **SPI**.

### Pin principali (come da sketch)

```text
I2C (touch): SDA=19, SCL=45
Backlight:   GFX_BL=38 (LEDC)
SWSPI (ST7701 cmd): CS=39, SCK=48, MOSI=47
RGB panel:   DE=18, VSYNC=17, HSYNC=16, PCLK=21
             R: 11,12,13,14,0
             G: 8,20,3,46,9,10
             B: 4,5,6,7,15
SD (SPI):    CS=42, SCK=48, MOSI=47, MISO=41 (FSPI)
```

> **Importante:** Non modificare i pin del display: il pilotaggio funziona già correttamente con questi valori.

---

## Librerie richieste

* **Arduino_GFX_Library** (display ST7701)
* **TAMC_GT911** (touch GT911)
* **SD** e **SPI** (incluse nel core ESP32)
* **JPEGENC** *(opzionale)* per l’export **JPEG**
  Se non presente, l’export avviene in **BMP** 24-bit.

---

## Compilazione e flash

* **Board**: ESP32S3 Dev Module (o profilo equivalente del tuo pannello)
* **Core ESP32**: consigliato ≥ **2.0.17**
* **PSRAM**: abilita se disponibile (non strettamente necessario qui)
* **Velocità upload**: usa un valore stabile per il tuo setup
* **Partition Scheme**: standard va bene

Compila e flasha lo sketch dal tuo IDE Arduino.

---

## Utilizzo

* **Disegno**: tocca una cella nella griglia per colorarla o sbiancarla (toggle).
* **Selezione colore**: tocca uno dei 5 “tasti” verticali nella colonna destra.
* **CLEAR**: tocca il pulsante a sinistra nella barra in basso (centrata).
* **EXPORT**: tocca il pulsante a destra nella barra in basso.

  * Durante l’esportazione compare il messaggio **“Esportazione…”**.
  * Al termine viene mostrato **“Export OK”** (oppure **“Export FAIL”**).

### File generati

* Nome file: `/pixel_<millis>.jpg` (con *JPEGENC*) oppure `/pixel_<millis>.bmp`
* Contenuto: solo l’area **griglia 400×480** (palette esclusa), con linee di griglia nere e riempimenti delle celle.

---

## Note tecniche

* **Debounce/edge**: il tocco attiva l’azione solo su *touch down* (transizione da non toccato → toccato), con **cooldown** per evitare più attivazioni ravvicinate.
* **Touch mapping**: le coordinate GT911 sono ruotate e mappate al portrait 480×480.
* **SD**: inizializzata su bus **FSPI** con i pin indicati sopra.

---

## Ispirazioni e riferimenti

* Ispirato ai lavori di **VolosR**:
  [https://github.com/VolosR/MakerfabsPixelArt/tree/main/PixelArt](https://github.com/VolosR/MakerfabsPixelArt/tree/main/PixelArt)
* Ispirato anche ai contenuti di **Survival Hacking**:
  [https://www.youtube.com/@SurvivalHacking](https://www.youtube.com/@SurvivalHacking)

Grazie a entrambi per l’ispirazione e la condivisione di idee.

---

## Troubleshooting

* **Messaggio “Più di una libreria trovata per SD.h”**
  È normale: l’IDE può avere sia la SD del core ESP32 che una esterna.
  Viene usata quella del core ESP32 (va bene così).
* **Export doppio o messaggi ripetuti**
  È già gestito con edge + cooldown. Se noti comportamenti anomali, puoi regolare il `TAP_COOLDOWN_MS` nello sketch.
* **SD non inizializzata**
  Controlla formattazione **FAT32** e la qualità/velocità della microSD.
* **Touch sfasato**
  Verifica orientamento (`ts.setRotation(0)`) e dimensioni GT911 impostate nell’oggetto `TAMC_GT911`.

---

## Licenza

Questo progetto è distribuito sotto licenza
**Creative Commons Attribuzione – Non commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi:

* **Condividere** — copiare e ridistribuire il materiale in qualsiasi formato o mezzo.
* **Adattare** — remixare, trasformare e sviluppare il materiale.

A condizione di:

* **Attribuzione** — devi fornire un’adeguata attribuzione all’autore originale (Davide Nasato / [davidegat](https://github.com/davidegat)), includendo un link alla licenza.
* **Non commerciale** — non puoi utilizzare il materiale per scopi commerciali.

👉 Testo completo della licenza:
[https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)
