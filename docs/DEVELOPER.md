# BubiCam - Documentazione Tecnica per Sviluppatori

Questo documento fornisce informazioni tecniche dettagliate per gli sviluppatori che vogliono comprendere, modificare o estendere BubiCam.

## Indice

1. [Panoramica Architetturale](#panoramica-architetturale)
2. [Sistema di Build](#sistema-di-build)
3. [Struttura del Progetto](#struttura-del-progetto)
4. [Classi Principali](#classi-principali)
5. [Flusso dei Messaggi](#flusso-dei-messaggi)
6. [Integrazione Media Kit](#integrazione-media-kit)
7. [Conversione Formati Video](#conversione-formati-video)
8. [Gestione USB](#gestione-usb)
9. [Aggiungere Nuove Funzionalita](#aggiungere-nuove-funzionalita)
10. [Debugging e Troubleshooting](#debugging-e-troubleshooting)
11. [Problemi Noti e Soluzioni](#problemi-noti-e-soluzioni)

---

## Panoramica Architetturale

BubiCam segue l'architettura tipica delle applicazioni Haiku con separazione tra:

- **Layer Applicazione** (`BubiCamApp`) - Entry point e gestione ciclo di vita
- **Layer UI** (`MainWindow`, classi `*View`) - Componenti dell'interfaccia utente
- **Layer Media** (`WebcamDevice`, `VideoConsumer`, etc.) - Integrazione Media Kit

```
┌─────────────────────────────────────────────────────────────────┐
│                         BubiCamApp                               │
│                       (BApplication)                             │
│                     Entry point, About dialog                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         MainWindow                               │
│                         (BWindow)                                │
│              Coordinatore centrale, gestione messaggi            │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Menu Bar  │ Webcam │ Format │ Control │ Tools │            │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    BSplitView (main)                        ││
│  │  ┌──────────────────┐   ┌──────────────────────────────┐   ││
│  │  │ Left Split       │   │ Right Split                  │   ││
│  │  │ ┌──────────────┐ │   │ ┌──────────────────────────┐ │   ││
│  │  │ │ Video Box    │ │   │ │ Tab View                 │ │   ││
│  │  │ │ [Toolbar]    │ │   │ │ ├─ Driver Info          │ │   ││
│  │  │ │ [Preview]    │ │   │ │ └─ Controls             │ │   ││
│  │  │ │ [Stats Bar]  │ │   │ └──────────────────────────┘ │   ││
│  │  │ └──────────────┘ │   │ ┌──────────────────────────┐ │   ││
│  │  │ ┌──────────────┐ │   │ │ Syslog Box              │ │   ││
│  │  │ │ VU Meter Box │ │   │ └──────────────────────────┘ │   ││
│  │  │ └──────────────┘ │   └──────────────────────────────┘   ││
│  │  └──────────────────┘                                       ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Status Bar                                                  ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│  WebcamRoster   │  │  WebcamDevice   │  │  ExportUtils    │
│  (Enumeration)  │  │  (Capture)      │  │  (File I/O)     │
└─────────────────┘  └─────────────────┘  └─────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│  VideoConsumer  │  │  AudioConsumer  │  │ USBVideoParser  │
│ (BBufferConsumer│  │ (BBufferConsumer│  │  (USB parsing)  │
│  + EventLooper) │  │  + EventLooper) │  │                 │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

---

## Sistema di Build

BubiCam utilizza il sistema di build standard `makefile-engine` di Haiku.

### Comandi di Build

```bash
# Build completa
make

# Build pulita
make clean && make

# Build con simboli di debug
make DEBUG=1

# Installazione nel sistema
make install
```

### Dipendenze (Librerie)

Definite nel `Makefile`:

| Libreria | Uso |
|----------|-----|
| `be` | Interface Kit, Application Kit, Storage Kit |
| `media` | Media Kit (BMediaRoster, BBufferConsumer) |
| `tracker` | Gestione tipi file MIME |
| `translation` | Salvataggio screenshot (PNG) |
| `localestub` | Stub per localizzazione |
| `device` | USB Kit (BUSBRoster) |

---

## Struttura del Progetto

```
BubiCam/
├── src/
│   ├── BubiCamApp.cpp/h         # Entry point applicazione
│   ├── MainWindow.cpp/h          # Finestra principale, menu, coordinamento
│   │
│   ├── views/                    # Componenti UI
│   │   ├── VideoPreviewView      # Visualizzazione video con aspect ratio
│   │   ├── DriverInfoView        # Informazioni driver (BTextView)
│   │   ├── SyslogView            # Monitor syslog con filtri
│   │   ├── VUMeterView           # Indicatore livello audio stereo
│   │   └── WebcamControlsView    # Controlli BParameterWeb
│   │
│   ├── webcam/                   # Layer Media Kit
│   │   ├── WebcamRoster          # Enumerazione dispositivi
│   │   ├── WebcamDevice          # Gestione singola webcam
│   │   ├── VideoConsumer         # Ricezione frame video
│   │   ├── AudioConsumer         # Ricezione dati audio
│   │   └── USBVideoParser        # Parsing descrittori USB UVC
│   │
│   └── utils/
│       └── ExportUtils           # Screenshot e export JSON/text
│
├── resources/
│   └── BubiCam.rdef              # Risorse app (icona HVIF, versione)
│
├── img/                          # Sorgenti icone SVG
├── objects.x86_64-cc13-release/  # Output build
├── Makefile                      # Build system
├── README.md                     # Documentazione utente
└── docs/                         # Documentazione tecnica
    ├── DEVELOPER.md              # Questo file
    ├── COMPARISON.md             # Comparazione con altri progetti
    ├── ROADMAP_v2.md             # Roadmap
    └── TASKS.md                  # Lista task
```

---

## Classi Principali

### BubiCamApp

**File:** `src/BubiCamApp.cpp/h`

Entry point dell'applicazione. Eredita da `BApplication`.

```cpp
class BubiCamApp : public BApplication {
    MainWindow*     fMainWindow;

    void ReadyToRun();      // Crea MainWindow
    void AboutRequested();  // Mostra dialog About
};
```

### MainWindow

**File:** `src/MainWindow.cpp/h`

Classe coordinatrice centrale che:
- Crea e gestisce tutte le view UI
- Gestisce comandi menu e shortcut tastiera
- Riceve messaggi dai consumer (frame video, livelli audio)
- Gestisce selezione webcam e stato cattura

**Membri Principali:**
```cpp
class MainWindow : public BWindow {
private:
    // Menu
    BMenuBar*           fMenuBar;
    BMenu*              fWebcamMenu;      // Lista webcam
    BMenu*              fFormatMenu;      // Formati video
    BMenu*              fControlMenu;     // Start/Stop
    BMenu*              fToolsMenu;       // Utility

    // Views
    VideoPreviewView*   fVideoPreview;    // Display video
    DriverInfoView*     fDriverInfo;      // Info driver
    SyslogView*         fSyslogView;      // Monitor log
    VUMeterView*        fVUMeter;         // Livelli audio
    WebcamControlsView* fWebcamControls;  // Parametri webcam
    BTabView*           fRightTabView;    // Tabs Info/Controls

    // Toolbar
    BButton*            fStartButton;
    BButton*            fStopButton;
    BButton*            fScreenshotButton;
    BButton*            fRefreshButton;

    // Stats Bar
    BStringView*        fStatsResolution;
    BStringView*        fStatsFPS;
    BStringView*        fStatsFrames;
    BStringView*        fStatsDropped;

    // Webcam management
    WebcamRoster*       fWebcamRoster;
    WebcamDevice*       fCurrentWebcam;
    bool                fIsPreviewActive;

    // Screenshot
    BFilePanel*         fSavePanel;
    BBitmap*            fLastFrame;
};
```

**Metodi Chiave:**
```cpp
void _BuildMenu();          // Crea struttura menu
void _BuildToolbar();       // Crea toolbar con pulsanti
void _BuildLayout();        // Costruisce layout con BLayoutBuilder
void _PopulateWebcamMenu(); // Aggiorna lista webcam
void _SelectWebcam(int32);  // Seleziona e avvia webcam
void _StartPreview();       // Inizia cattura
void _StopPreview();        // Ferma cattura
void _UpdateStatsBar();     // Aggiorna statistiche (FPS, frame, etc)
void _UpdateToolbarState(); // Enable/disable pulsanti
void _RestartMediaServices();// Riavvia media_server
void MessageReceived(BMessage*); // Handler messaggi
```

### WebcamRoster

**File:** `src/webcam/WebcamRoster.cpp/h`

Enumera i dispositivi video disponibili tramite Media Kit.

```cpp
class WebcamRoster {
public:
    status_t        EnumerateDevices();
    int32           CountDevices() const;
    WebcamDevice*   DeviceAt(int32 index) const;
    WebcamDevice*   DeviceByName(const char* name) const;
    void            Clear();

private:
    bool            _IsVideoProducer(const dormant_node_info& info);
    void            _EnumerateDevVideoDevices();

    BObjectList<WebcamDevice> fDevices;
    mutable BLocker fLock;
};
```

**Logica Enumerazione:**
1. Chiama `BMediaRoster::GetDormantNodes()` con `B_BUFFER_PRODUCER | B_PHYSICAL_INPUT`
2. Filtra per nomi contenenti "video", "camera", "uvc", "webcam"
3. Istanzia ogni nodo dormant per verificare funzionamento
4. Crea oggetti `WebcamDevice` per ogni dispositivo valido

### WebcamDevice

**File:** `src/webcam/WebcamDevice.cpp/h`

Rappresenta una singola webcam e gestisce le connessioni Media Kit.

```cpp
class WebcamDevice {
public:
    // Identificazione
    const char*     Name() const;
    const char*     DevicePath() const;

    // Info USB
    uint16          VendorID() const;
    uint16          ProductID() const;
    const char*     VendorName() const;

    // Info Driver
    const char*     DriverName() const;
    const char*     DriverVersion() const;

    // Capacita video
    bool            SupportsVideo() const;
    const BObjectList<VideoFormat>& SupportedFormats() const;

    // Capacita audio
    bool            SupportsAudio() const;
    float           AudioSampleRate() const;

    // Media Kit
    bool            IsNodeInstantiated() const;
    const media_node& MediaNode() const;

    // Controllo cattura
    status_t        StartCapture(BLooper* target);
    void            StopCapture();
    bool            IsCapturing() const;

    // Statistiche
    uint32          FramesCaptured() const;
    uint32          FramesDropped() const;
    float           CurrentFPS() const;

private:
    // Connessioni Media Kit
    VideoConsumer*  fVideoConsumer;
    AudioConsumer*  fAudioConsumer;
    media_output    fVideoOutput;
    media_input     fVideoInput;
    bool            fVideoConnected;
    bool            fIsCapturing;
    BLooper*        fTarget;
};
```

**Flusso StartCapture:**
1. Verifica nodo istanziato
2. Crea `VideoConsumer` con target BLooper
3. Ottiene output video dal producer
4. Connette producer → consumer
5. (Opzionale) Setup connessione audio
6. Sincronizza time source
7. Avvia nodi

### VideoConsumer

**File:** `src/webcam/VideoConsumer.cpp/h`

`BBufferConsumer` che riceve frame video dalla webcam. **IMPORTANTE**: Implementa
`BBufferGroup` in stile CodyCam per massima compatibilita con i driver.

```cpp
class VideoConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
    // BBufferConsumer
    status_t        AcceptFormat(...);      // Accetta tutti i formati
    void            BufferReceived(BBuffer*);// Riceve frame
    status_t        Connected(...);         // Gestisce connessione + BBufferGroup
    void            Disconnected(...);      // Gestisce disconnessione

    // Buffer management (CodyCam-style) - CRITICO per compatibilita driver
    status_t        CreateBuffers(const media_format& format);
    void            DeleteBuffers();

    // Statistiche
    uint32          FramesReceived() const;
    uint32          FramesDropped() const;
    float           CurrentFPS() const;

private:
    void            _HandleBuffer(BBuffer*);
    void            _ConvertBuffer(BBuffer*, BBitmap*);
    void            _ConvertYUV422ToBGRA(...);
    void            _ConvertYUV420ToBGRA(...);
    void            _SendFrameToTarget(BBitmap*);

    BLooper*        fTarget;            // MainWindow
    uint32          fFrameMessage;      // MSG_FRAME_RECEIVED

    // CodyCam-style buffer group - CRITICO!
    BBufferGroup*   fBuffers;           // Gruppo buffer condiviso con producer
    BBitmap*        fBitmap[3];         // Triple buffering
    BBuffer*        fBufferMap[3];      // Mapping buffer -> bitmap
    BBitmap*        fDisplayBitmap;     // Per conversione formato
};
```

#### Architettura BBufferGroup (Stile CodyCam)

La differenza **CRITICA** tra BubiCam e CodyCam/applicazioni che non funzionano
con alcuni driver e l'uso di `BBufferGroup` e `SetOutputBuffersFor()`:

```cpp
// In Connected():
status_t status = CreateBuffers(withFormat);
if (status == B_OK) {
    // QUESTA CHIAMATA E FONDAMENTALE!
    // Passa i nostri buffer al producer invece di lasciare
    // che il producer crei i propri (che potrebbe non fare correttamente)
    BBufferConsumer::SetOutputBuffersFor(producer, fDestination,
        fBuffers, &userData, &changeTag, true);
}

// CreateBuffers() crea BBitmap con memoria condivisibile:
fBitmap[j] = new BBitmap(bounds, colorspace, false, true);
// Il 4o parametro 'true' = "accepts child views" = memoria contiguous
info.area = area_for(fBitmap[j]->Bits());  // Ottiene area memoria
fBuffers->AddBuffer(info);                  // Aggiunge al gruppo
```

**Schema Memoria Buffer:**
```
                    BBufferGroup (3 buffer)
                           │
          ┌────────────────┼────────────────┐
          │                │                │
      Buffer[0]        Buffer[1]        Buffer[2]
          │                │                │
    area_for(bits)   area_for(bits)   area_for(bits)
          │                │                │
      BBitmap[0]       BBitmap[1]       BBitmap[2]
          │                │                │
          └───────┬────────┴────────┬───────┘
                  │                 │
            Producer             Consumer
         (scrive qui)      (legge da qui)
```

### Views

| Classe | Eredita Da | Descrizione |
|--------|-----------|-------------|
| `VideoPreviewView` | `BView` | Display video con mantenimento aspect ratio, statistiche overlay opzionali |
| `DriverInfoView` | `BTextView` | Informazioni device/driver formattate |
| `SyslogView` | `BTextView` | Monitor `/var/log/syslog` con filtri USB/media e colori |
| `VUMeterView` | `BView` | Meter audio stereo con peak hold |
| `WebcamControlsView` | `BView` | Render controlli `BParameterWeb` |

---

## Flusso dei Messaggi

### Costanti Messaggi (MainWindow.h)

```cpp
enum {
    MSG_WEBCAM_SELECTED   = 'wcsl',  // Utente seleziona webcam
    MSG_WEBCAM_START      = 'wcst',  // Pulsante Start
    MSG_WEBCAM_STOP       = 'wcsp',  // Pulsante Stop
    MSG_REFRESH_DEVICES   = 'rfrd',  // Refresh lista
    MSG_FRAME_RECEIVED    = 'frcv',  // Frame da consumer
    MSG_AUDIO_LEVEL       = 'audl',  // Livello audio da consumer
    MSG_SCREENSHOT        = 'scsh',  // Cattura screenshot
    MSG_SCREENSHOT_SAVED  = 'scsv',  // Screenshot salvato
    MSG_EXPORT_INFO       = 'expi',  // Export testo
    MSG_EXPORT_INFO_JSON  = 'expj',  // Export JSON
    MSG_FORMAT_SELECTED   = 'fmsl',  // Formato selezionato
    MSG_CLEAR_SYSLOG      = 'clsl',  // Pulisci syslog
    MSG_TOGGLE_CONTROLS   = 'tgct',  // Mostra tab controlli
    MSG_RESTART_MEDIA     = 'rmed',  // Riavvia media services
};
```

### Flusso Ricezione Frame

```
┌───────────────┐      BBuffer        ┌───────────────┐
│ Webcam Node   │ ──────────────────► │ VideoConsumer │
│ (Producer)    │                     │ (Consumer)    │
└───────────────┘                     └───────┬───────┘
                                              │
                                    BufferReceived()
                                    _ConvertBuffer()
                                    _SendFrameToTarget()
                                              │
                                    BMessage(MSG_FRAME_RECEIVED)
                                    + pointer to BBitmap
                                              │
                                              ▼
                                      ┌───────────────┐
                                      │  MainWindow   │
                                      │ MessageReceived()
                                      └───────┬───────┘
                                              │
                      ┌───────────────────────┼───────────────────────┐
                      ▼                       ▼                       ▼
               VideoPreviewView         _UpdateStatsBar()        fLastFrame
               SetFrame(bitmap)        (aggiorna stats bar)     (per screenshot)
               Invalidate()
```

### Flusso Audio

```
┌───────────────┐      BBuffer        ┌───────────────┐
│ Audio Node    │ ──────────────────► │ AudioConsumer │
│ (Producer)    │                     │ (Consumer)    │
└───────────────┘                     └───────┬───────┘
                                              │
                                    Calcola RMS left/right
                                    BMessage(MSG_AUDIO_LEVEL)
                                              │
                                              ▼
                                      ┌───────────────┐
                                      │  MainWindow   │
                                      └───────┬───────┘
                                              │
                                              ▼
                                       ┌───────────────┐
                                       │  VUMeterView  │
                                       │ SetLevel(l,r) │
                                       │ Invalidate()  │
                                       └───────────────┘
```

---

## Integrazione Media Kit

### Strategia Connessione Nodi

BubiCam prova multiple strategie per connettersi alle webcam (in `WebcamDevice::_SetupVideoConnection()`):

| Strategia | Formato | Note |
|-----------|---------|------|
| 0 | 320x240 B_RGB32 | Stile CodyCam, massima compatibilita |
| 1 | 640x480 B_RGB32 | Risoluzione comune |
| 2 | Formato dichiarato dal producer | Usa preferenze driver |
| 3 | 320x240 YCbCr422 | Fallback YUV |

### Negoziazione Formato

`VideoConsumer::AcceptFormat()` e intenzionalmente permissivo:

```cpp
status_t VideoConsumer::AcceptFormat(const media_destination& dest,
    media_format* format)
{
    // Accetta TUTTO - siamo uno strumento di testing
    // Driver buggy potrebbero avere formati non standard
    return B_OK;
}
```

### Sincronizzazione Temporale

Tutti i nodi condividono una time source comune:

```cpp
// Ottieni time source di sistema
media_node timeSource;
roster->GetTimeSource(&timeSource);

// Imposta per producer e consumer
roster->SetTimeSourceFor(fMediaNode.node, timeSource.node);
roster->SetTimeSourceFor(fVideoConsumer->Node().node, timeSource.node);

// Calcola tempo di avvio sincronizzato
BTimeSource* ts = roster->MakeTimeSourceFor(timeSource);
bigtime_t startTime = ts->Now() + 50000;  // +50ms

// Avvia nodi
roster->StartNode(fMediaNode, startTime);
roster->StartNode(fVideoConsumer->Node(), startTime);
```

### Cleanup Connessioni

**IMPORTANTE:** Il cleanup deve gestire race condition:

```cpp
void WebcamDevice::StopCapture()
{
    if (!fIsCapturing)
        return;
    fIsCapturing = false;

    BMediaRoster* roster = BMediaRoster::Roster();
    if (roster != NULL) {
        // CRITICO: Salva copie locali prima di azzerare membri
        VideoConsumer* videoConsumer = fVideoConsumer;
        AudioConsumer* audioConsumer = fAudioConsumer;
        media_node producerNode = fMediaNode;

        fVideoConsumer = NULL;  // Previene accessi concorrenti
        fAudioConsumer = NULL;

        // Usa copie locali per operazioni
        if (videoConsumer != NULL) {
            roster->StopNode(videoConsumer->Node(), 0, true);
        }
        roster->StopNode(producerNode, 0, true);

        // Ripristina per cleanup
        fVideoConsumer = videoConsumer;
        fAudioConsumer = audioConsumer;
    }

    _TeardownConnections();
}
```

---

## Conversione Formati Video

`VideoConsumer::_ConvertBuffer()` supporta:

| Formato | Metodo Conversione |
|---------|-------------------|
| `B_RGB32` | Copia diretta |
| `B_RGB24` | Espansione 24→32 bit |
| `B_YCbCr422` (YUYV) | `_ConvertYUV422ToBGRA()` |
| `B_YCbCr420` (I420) | `_ConvertYUV420ToBGRA()` |

### Conversione YUV422 → BGRA

```cpp
void VideoConsumer::_ConvertYUV422ToBGRA(const uint8* src, uint8* dst,
    int32 width, int32 height)
{
    // YUYV: Y0 U Y1 V → 2 pixel BGRA
    for (int32 y = 0; y < height; y++) {
        for (int32 x = 0; x < width; x += 2) {
            int32 y0 = src[0];
            int32 u  = src[1] - 128;
            int32 y1 = src[2];
            int32 v  = src[3] - 128;

            // Pixel 0
            dst[0] = CLAMP(y0 + 1.772 * u);          // B
            dst[1] = CLAMP(y0 - 0.344 * u - 0.714 * v); // G
            dst[2] = CLAMP(y0 + 1.402 * v);          // R
            dst[3] = 255;                             // A

            // Pixel 1 (condivide U,V)
            dst[4] = CLAMP(y1 + 1.772 * u);
            dst[5] = CLAMP(y1 - 0.344 * u - 0.714 * v);
            dst[6] = CLAMP(y1 + 1.402 * v);
            dst[7] = 255;

            src += 4;
            dst += 8;
        }
    }
}
```

---

## Gestione USB

### Raccolta Info USB

`WebcamDevice::_GatherUSBInfo()` usa `BUSBRoster` per ottenere:
- Vendor ID / Product ID
- Vendor Name / Product Name
- Serial Number
- USB Version
- Device Class / Subclass / Protocol

### Parsing Descrittori UVC

`USBVideoParser` estrae informazioni dai descrittori USB Video Class:
- Formati supportati (MJPEG, uncompressed)
- Risoluzioni e frame rate
- Controlli camera (brightness, contrast, etc.)

---

## Aggiungere Nuove Funzionalita

### Aggiungere una Nuova View

1. Crea `src/views/MyNewView.cpp/h`
2. Aggiungi a `SRCS` nel `Makefile`
3. Include in `MainWindow.h`
4. Istanzia in `MainWindow::_BuildLayout()`
5. Aggiungi al layout appropriato

### Aggiungere una Nuova Voce Menu

1. Aggiungi costante messaggio in `MainWindow.h`:
   ```cpp
   MSG_MY_ACTION = 'myac',
   ```

2. Crea menu item in `MainWindow::_BuildMenu()`:
   ```cpp
   menu->AddItem(new BMenuItem("My Action",
       new BMessage(MSG_MY_ACTION), 'M'));
   ```

3. Gestisci in `MainWindow::MessageReceived()`:
   ```cpp
   case MSG_MY_ACTION:
       _DoMyAction();
       break;
   ```

### Aggiungere un Pulsante Toolbar

1. Aggiungi membro `BButton*` in `MainWindow.h`
2. Crea pulsante in `MainWindow::_BuildToolbar()`
3. Aggiungi al layout toolbar
4. Aggiorna stato in `MainWindow::_UpdateToolbarState()`

### Supportare un Nuovo Formato Video

Aggiungi conversione in `VideoConsumer::_ConvertBuffer()`:

```cpp
case B_MY_FORMAT:
    _ConvertMyFormatToBGRA(src, dst, width, height);
    break;
```

---

## Debugging e Troubleshooting

### Abilitare Log

Video/audio consumer stampano debug su stderr:

```bash
./objects.x86_64-cc13-release/BubiCam 2>&1 | tee debug.log
```

### Punti di Debug Chiave

| File:Funzione | Cosa Mostra |
|---------------|-------------|
| `WebcamRoster::EnumerateDevices()` | Lista dispositivi rilevati |
| `WebcamDevice::StartCapture()` | Sequenza connessione |
| `VideoConsumer::BufferReceived()` | Arrivo frame |
| `VideoConsumer::_ConvertBuffer()` | Conversione formato |
| `WebcamDevice::StopCapture()` | Cleanup connessioni |

### Comandi Utili Haiku

```bash
# Lista nodi Media Kit
listimage media_server
media_client nodes

# Lista dispositivi USB
listusb

# Log sistema
tail -f /var/log/syslog | grep -i "usb\|video\|media\|uvc"

# Riavvia media services
kill media_server media_addon_server
# oppure da BubiCam: Tools > Restart Media Services
```

---

## Comparazione con Cortex e CodyCam

Vedere [COMPARISON.md](COMPARISON.md) per un'analisi dettagliata delle differenze
tra BubiCam, Cortex e CodyCam nella gestione della riproduzione video.

### Problema Originale

BubiCam originalmente non funzionava con driver webcam che funzionavano
perfettamente in Cortex. L'analisi ha rivelato che:

1. **Cortex** e piu tollerante perche usa un'architettura modulare tramite NodeManager
2. **CodyCam** usa `BBufferGroup` e `SetOutputBuffersFor()` per fornire buffer al producer
3. **BubiCam (vecchio)** lasciava che il producer creasse i propri buffer (che alcuni driver non fanno)

### Soluzione Implementata

Il `VideoConsumer` e stato riscritto per implementare l'architettura CodyCam:

1. Crea un `BBufferGroup` con 3 buffer nel metodo `CreateBuffers()`
2. Ogni buffer e associato a una `BBitmap` con memoria condivisibile (`area_for()`)
3. In `Connected()`, chiama `SetOutputBuffersFor()` per passare i buffer al producer
4. Il producer ora scrive nei nostri buffer invece di doverne creare di propri

Questa modifica rende BubiCam compatibile con driver che non implementano
completamente la creazione autonoma dei buffer.

---

## Problemi Noti e Soluzioni

### "Name not found" quando si seleziona webcam

**Causa:** Nodo media gia in uso da crash precedente.

**Soluzione:** Menu Tools > "Restart Media Services" oppure:
```bash
kill media_server media_addon_server
```

### "No video outputs available"

**Causa:** Driver non espone output video.

**Soluzione:**
1. Verifica in Media preferences
2. Controlla che webcam sia supportata
3. Guarda syslog per errori driver

### Video nero

**Causa:** Formato non supportato o conversione mancante.

**Debug:**
```bash
./BubiCam 2>&1 | grep "format\|color"
```

**Soluzione:** Aggiungi conversione in `VideoConsumer::_ConvertBuffer()`

### Crash all'uscita

**Causa:** Race condition nel cleanup nodi.

**Soluzione:** Implementata in `WebcamDevice::StopCapture()` salvando copie locali dei puntatori prima di azzerarli.

### FPS basso

**Causa:** Conversione formato lenta.

**Soluzione:**
1. Ottimizza `_ConvertBuffer()` (SIMD, lookup tables)
2. Riduci risoluzione
3. Usa formato nativo del driver

---

## Code Style

BubiCam segue lo stile Haiku:

- Tab per indentazione
- `fMemberVariable` per membri classe
- `_PrivateMethod()` per metodi privati
- Braces e spaziatura stile Haiku

```cpp
void
ClassName::MethodName()
{
    if (condition) {
        // codice
    } else {
        // altro
    }
}
```

---

## Contribuire

1. Fork del repository
2. Crea branch per feature
3. Segui stile codice esistente
4. Testa con multiple webcam se possibile
5. Invia pull request

Per domande, apri issue sul repository del progetto.
