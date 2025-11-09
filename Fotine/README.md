# Fotine

**Fotine** è un **photo frame per ESP32-S3 Panel-4848S040** che scarica e visualizza automaticamente immagini da internet su un display 480×480.

<img src="https://github.com/user-attachments/assets/c42f08eb-92c8-4329-9cfa-1c32d923c6d2" width="500">
     
## Descrizione

Lo sketch:
* si connette al Wi-Fi (salvato in NVS o configurabile tramite captive portal);
* sincronizza data e ora con **NTP**;
* scarica immagini da sorgenti predefinite;
* verifica il formato **JPEG baseline** prima della visualizzazione;
* mostra le immagini salvate su SD aggiornandole ogni **5 minuti**.

## Hardware compatibile
* **ESP32-S3 Panel-4848S040** (display ST7701, 480×480, type9)

## Librerie richieste
Installabili dal **Library Manager** di Arduino:
* `Arduino_GFX_Library`
* `WiFi`
* `WebServer`
* `DNSServer`
* `HTTPClient`
* `TJpg_Decoder`
* `SD`

## Impostazioni consigliate per Arduino IDE
Nel **menu Strumenti**, selezionare:

* **PSRAM:** Enabled
* **USB Mode:** Hardware CDC and JTAG
* **Upload Speed:** 921600 bps
* **Partition Scheme:** Default (16 MB with SPIFFS)
* **Programmazione:** USB-OTG / UART integrata (a seconda della versione del modulo)

## Note

* Il Wi-Fi resta salvato in memoria: non serve riconfigurarlo dopo un nuovo upload.
* Le immagini non compatibili (non baseline) vengono scartate automaticamente.

## Licenza

**Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)**
Puoi **copiare, modificare e ridistribuire** questo progetto a condizione di:

* **citare l’autore originale**,
* **non usarlo a fini commerciali**,
* **mantenere la stessa licenza** su eventuali derivati.
