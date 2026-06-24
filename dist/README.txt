================================================================================

    ____        _     _  _____
   |  _ \      | |   (_)/ ____|
   | |_) |_   _| |__  _| |     __ _ _ __ ___
   |  _ <| | | | '_ \| | |    / _` | '_ ` _ \
   | |_) | |_| | |_) | | |___| (_| | | | | | |
   |____/ \__,_|_.__/|_|\_____\__,_|_| |_| |_|

                    Webcam Driver Tester for Haiku OS

================================================================================

                        ~~~  Made in Venezia, Italia  ~~~

                              .     .     .
                           .  |\   /|   /|  .
                         .   |  \_/  \_/ |   .
                        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                           ~   Gondola Software   ~
                        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

================================================================================

                              GUIDA ALL'USO

================================================================================

INTRODUZIONE
------------
BubiCam e' un'applicazione nativa per Haiku OS progettata per testare e
debuggare i driver delle webcam USB. Mostra video live, registra su disco,
espone la camera via HTTP e MCP, ed esegue una test suite completa contro il
driver.


AVVIO
-----
Doppio click su BubiCam oppure da terminale:

    ./BubiCam

Per modalita' server senza GUI:

    ./BubiCam --headless


INTERFACCIA PRINCIPALE
----------------------

  +------------------------------------------------------------------+
  | File | Webcam | Formato | Controllo | Tools | Test | Vista       |
  +------------------------------------------------------------------+
  |  [Refresh] [Start] [Stop] [Screenshot] [Record] [LED]            |
  +------------------------------------------------------------------+
  |                           |                                      |
  |   ANTEPRIMA VIDEO         |   Tabs:                              |
  |   - zoom 1x..8x           |   - Info Driver                      |
  |   - histogram overlay     |   - Controlli (luminosita' ecc.)     |
  |   - A/B compare           |   - Driver Test (stress, latenza)    |
  |   - griglia overlay       |   - Ispettore pacchetti USB          |
  |   - fullscreen (Invio)    |   - Syslog (filtri regex)            |
  |                           |                                      |
  +------------------------------------------------------------------+
  |  VU Meter L/R                                                    |
  +------------------------------------------------------------------+
  |  Risoluzione: 640x480 | FPS: 30 | Frames: 1234 | Drop: 0         |
  +------------------------------------------------------------------+


FUNZIONI PRINCIPALI
-------------------

VIDEO
  - Preview live, statistiche FPS/frame/drop
  - Decodifica MJPEG (libjpeg-turbo)
  - Conversioni YUV422/YUV420/NV12/NV21/UYVY/B_GRAY8 (ottimizzate SSE2)
  - Zoom (1x..8x) con rotellina mouse, pan con drag
  - Histogram RGB overlay (Cmd+H)
  - Confronto A/B (Cmd+B / Cmd+Shift+B)
  - Griglia overlay (regola dei terzi, mirino)
  - Modalita' fullscreen (Invio)
  - Anteprima floating sempre in primo piano

REGISTRAZIONE
  - Video AVI Motion JPEG con traccia audio
  - Time-lapse (intervallo configurabile)
  - Buffer circolare "salva ultimi N secondi"
  - Screenshot PNG (Cmd+P)

AUDIO
  - VU meter per mic webcam
  - Selezione sorgente audio (webcam / system input / nessuna)
  - Audio mixato nell'AVI registrato

TESTING DRIVER
  - Stress test (cicli start/stop)
  - Test latenza (ms cattura-display)
  - Benchmark formati
  - Test memory leak
  - Cycle test (robustezza connect/disconnect)
  - Export risultati CSV / JSON / report diagnostico

INFO DISPOSITIVO
  - Nome driver, versione, parsing descrittori USB (UVC)
  - Monitor syslog con filtro regex
  - Ispettore pacchetti USB con hex dump
  - Controlli webcam via BParameterWeb + preset

INTEGRAZIONE
  - Server MCP su porta 9847 (per Claude Code)
  - Replicant Deskbar con LED di stato
  - Replicant Desktop con preview live
  - Server streaming MJPEG via HTTP
  - Virtual webcam (BMediaAddOn)
  - Notifiche di sistema
  - Scripting hey
  - Localizzazione: EN, IT, DE, ZH, JA
  - Tema sistema (chiaro/scuro)


SCORCIATOIE TASTIERA
--------------------
  Cmd+R         Aggiorna dispositivi
  Cmd+S         Avvia preview
  Cmd+T         Ferma preview
  Cmd+P         Screenshot
  Cmd+E         Esporta info driver
  Cmd+H         Toggle histogram
  Cmd+G         Toggle griglia overlay
  Cmd+B         Cattura frame di riferimento
  Cmd+Shift+B   Modalita' A/B compare
  Cmd+0         Reset zoom
  Cmd+L         Pulisci syslog
  Cmd+Shift+M   Riavvia media services
  Invio         Fullscreen
  Escape        Esci da fullscreen


USO TIPICO
----------
1. Avvia BubiCam
2. Scegli la tua webcam dal menu Webcam
3. Clicca Start (oppure Cmd+S)
4. Usa i tab a destra per ispezionare il comportamento del driver
5. Esegui i test dal menu Tests se stai indagando problemi del driver


RISOLUZIONE PROBLEMI
--------------------

PROBLEMA: "Name not found" quando selezioni una webcam
  -> Menu Tools -> Restart Media Services

PROBLEMA: Nessuna webcam trovata
  -> Verifica output di `listusb` e tab Syslog

PROBLEMA: Video nero / nessun frame
  -> Controlla il tab Syslog per errori del driver
  -> Prova Tools -> Restart Media Services

PROBLEMA: FPS basso o drop frame elevato
  -> Seleziona una risoluzione inferiore dal menu Formato
  -> Verifica banda USB (usa hub USB 3 per alta risoluzione)

PROBLEMA: L'app sembra bloccata
  -> Aspetta fino a 15 secondi: l'emergency exit watchdog
     terminera' forzatamente il processo. Poi riavvia.


REQUISITI DI SISTEMA
--------------------
- Haiku OS R1/beta5 o successivo (x86_64)
- Webcam USB compatibile UVC
- Media Kit funzionante
- libjpeg-turbo (per webcam MJPEG)


================================================================================

                              INFORMAZIONI

================================================================================

Versione:      2.0
Licenza:       MIT License
Repository:    https://github.com/atomozero/BubiCam

Sviluppato con amore a Venezia, Italia
Dove l'acqua incontra la tecnologia!

                              ,___,
                              [O.o]
                              /)__)
                              -"--"-
                           BubiCam Team

================================================================================
                    Copyright (c) 2024-2026 BubiCam Contributors
================================================================================
