# Pongino

## Descrizione (Italiano)
Pongino è un clone di Arkanoid progettato per il modulo ESP32-S3 con schermo RGB 480×480 basato su driver ST7701 e digitalizzatore capacitivo GT911. Il gioco gira sul pannello 4848S040 utilizzando la libreria Arduino_GFX_Library per la gestione video e il controller touch per il puntamento.

### Logica di gioco
* **Campo da gioco:** lo schermo viene popolato da uno sfondo stellato animato e da una griglia di mattoni colorati in 6 righe e 8 colonne. Ogni mattone mantiene lo stato di danno e può cambiare colore durante gli effetti di palette.
* **Paddle e controllo:** il paddle arrotondato segue il dito sul touch capacitivo, con gestione di bonus che ne aumentano o riducono la larghezza. Effetti di lampeggio segnalano temporaneamente le variazioni di dimensione.
* **Palla:** parte poggiata sul paddle e viene lanciata dal giocatore. La velocità cresce progressivamente fino a un limite massimo e viene aggiornata a ogni impatto con paddle, muri o mattoni, applicando controlli di collisione e correzioni per evitare sovrapposizioni.
* **Mattoni e effetti:** i mattoni possono contenere missili e bonus; quando vengono colpiti, generano particelle e possono rilasciare gemme (power-up) o penalità. Un sistema di shuffle periodico anima i mattoni "hover" per dare movimento alla griglia.
* **Bonus e penalità:** bonus cadenti ampliano il paddle e offrono vantaggi temporanei, mentre i power-down ne riducono la larghezza o introducono ostacoli. Gli effetti hanno durate prestabilite e scadono automaticamente.
* **Missili:** alcuni mattoni lanciano missili diretti verso il paddle; il giocatore deve evitarli o intercettarli, gestiti con collisioni dedicate.
* **Stati di gioco:** il gioco tiene traccia di vite, punteggio, vittoria e game over. Alla fine del livello vengono mostrati messaggi e l'HUD viene aggiornato con logiche anti-flicker per mantenere fluida la visualizzazione.

### Requisiti hardware
Il progetto è pensato per il modulo ESP32-S3 4848S040 con:
* Display RGB da 480×480 pixel pilotato dal driver ST7701 (type9) tramite bus SWSPI e pannello RGB.
* Touchscreen capacitivo GT911 collegato via I²C.
* Retroilluminazione controllata dal pin 38.

## Description (English)
Pongino is an Arkanoid-style clone targeting the ESP32-S3 module with a 480×480 RGB display driven by an ST7701 controller and a GT911 capacitive touch panel. The game runs on the 4848S040 board and relies on Arduino_GFX_Library for video output and on the touch controller for user input.

### Game logic
* **Playfield:** the screen features an animated starfield background and a brick layout of 6 rows by 8 columns. Each brick stores damage state and can swap palette colors when special effects are active.
* **Paddle and control:** the rounded paddle follows the user's finger on the capacitive touch. Power-ups can enlarge the paddle while penalties shrink it, with blinking effects highlighting the temporary state.
* **Ball:** it starts on top of the paddle and is launched by the player. Speed increases gradually up to a cap and is updated on every collision with the paddle, walls, or bricks, applying overlap corrections for smooth rebounds.
* **Bricks and effects:** bricks may contain missiles or bonuses; when hit they spawn particle bursts and can drop gems (power-ups) or harmful items. A periodic shuffle animates "hover" bricks to keep the grid lively.
* **Bonuses and penalties:** falling bonuses grant temporary advantages such as a wider paddle, while power-downs reduce its width or add challenges. Each effect has a fixed duration and expires automatically.
* **Missiles:** selected bricks fire missiles aimed at the paddle; the player must dodge or block them, with dedicated collision checks.
* **Game states:** the engine tracks lives, score, victory, and game over. Once the level is cleared, status messages are displayed and the HUD uses anti-flicker updates for smooth visuals.

### Hardware requirements
The project targets the ESP32-S3 4848S040 module featuring:
* 480×480 RGB display driven by an ST7701 (type9) controller via SWSPI bus and RGB panel interface.
* GT911 capacitive touchscreen connected over I²C.
* Backlight controlled on GPIO 38.

