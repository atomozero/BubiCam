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
BubiCam e' un'applicazione nativa per Haiku OS progettata per testare i driver
delle webcam USB. Permette di visualizzare il flusso video, controllare i
parametri della webcam e monitorare lo stato del driver.


AVVIO
-----
Doppio click su BubiCam oppure da terminale:

    ./BubiCam


INTERFACCIA PRINCIPALE
----------------------

  +------------------------------------------------------------------+
  | File | Webcam | Controllo | Strumenti | Aiuto                    |
  +------------------------------------------------------------------+
  |  [Refresh]  [Start]  [Stop]  [Screenshot]                        |
  +------------------------------------------------------------------+
  |                           |                                      |
  |                           |   [Tab: Info Driver]                 |
  |    ANTEPRIMA VIDEO        |   - Nome dispositivo                 |
  |                           |   - Formati supportati               |
  |    (qui appare il         |   - Stato connessione                |
  |     flusso video)         |                                      |
  |                           |   [Tab: Syslog]                      |
  |                           |   - Messaggi di debug                |
  |                           |                                      |
  +------------------------------------------------------------------+
  |  Risoluzione: 640x480  |  FPS: 30  |  Frames: 1234  |  Drop: 0   |
  +------------------------------------------------------------------+


MENU E FUNZIONI
---------------

MENU FILE:
  - Screenshot (Alt+S)     Salva un'immagine del frame corrente in PNG
  - Esporta Info           Esporta le informazioni del driver in TXT
  - Esporta Info JSON      Esporta le informazioni in formato JSON
  - Esci (Alt+Q)           Chiude l'applicazione

MENU WEBCAM:
  - Lista delle webcam rilevate nel sistema
  - Seleziona una webcam per attivarla

MENU CONTROLLO:
  - Avvia Anteprima        Inizia la cattura video
  - Ferma Anteprima        Interrompe la cattura
  - Aggiorna Dispositivi   Riscansiona le webcam disponibili
  - Formato Video          Seleziona risoluzione e formato

MENU STRUMENTI:
  - Riavvia Media Server   Utile se la webcam non risponde
  - Mostra Controlli       Apre il pannello controlli webcam


USO TIPICO
----------

1. Avvia BubiCam

2. Dal menu "Webcam", seleziona la tua webcam dalla lista

3. Clicca "Avvia Anteprima" o premi il pulsante Start nella toolbar

4. L'anteprima video appare nel pannello sinistro

5. Usa il tab "Info Driver" per vedere i dettagli tecnici

6. Usa "Screenshot" per salvare un frame


RISOLUZIONE PROBLEMI
--------------------

PROBLEMA: "Name not found" o webcam non rilevata
SOLUZIONE: Menu Strumenti -> Riavvia Media Server

PROBLEMA: Video nero o nessun frame
SOLUZIONE:
  1. Verifica che la webcam sia collegata
  2. Controlla il tab Syslog per messaggi di errore
  3. Prova a riavviare il Media Server

PROBLEMA: FPS basso o frame drop alto
SOLUZIONE:
  1. Seleziona una risoluzione inferiore dal menu Formato
  2. Chiudi altre applicazioni che usano la webcam


FORMATI VIDEO SUPPORTATI
------------------------
- B_RGB32      (32-bit RGB)
- B_RGB24      (24-bit RGB)
- B_YCbCr422   (YUYV)
- B_YCbCr420   (I420/YUV420P)


REQUISITI DI SISTEMA
--------------------
- Haiku OS (x86_64)
- Webcam USB compatibile UVC
- Media Kit funzionante


================================================================================

                              INFORMAZIONI

================================================================================

Versione:      1.0
Licenza:       MIT License
Repository:    https://github.com/user/BubiCam

Sviluppato con amore a Venezia, Italia
Dove l'acqua incontra la tecnologia!

                              ,___,
                              [O.o]
                              /)__)
                              -"--"-
                           BubiCam Team

================================================================================
                    Copyright (c) 2024 BubiCam Contributors
================================================================================
