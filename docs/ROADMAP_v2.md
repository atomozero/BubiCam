# BubiCam - Roadmap

Documento di pianificazione e stato delle funzionalità di BubiCam.

**Versione attuale:** 2.0
**Ultimo aggiornamento:** 2026-07-01
**Stato:** In sviluppo attivo

---

## Legenda Stati

- [ ] Non iniziato
- [~] In sviluppo
- [x] Completato
- [!] Bloccato / Problema
- [-] Cancellato / Rimandato

---

## 1. Registrazione Video/Audio

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Registrazione video | AVI Motion JPEG con `VideoRecorder` |
| [x] | Registrazione audio | Cattura microfono webcam, integrata nell'AVI |
| [x] | Time-lapse | Scatto a intervalli configurabili (PNG sequence) |
| [x] | Buffer circolare | "Salva ultimi N secondi", buffer RAM in `VideoRecorder` |
| [x] | Selezione codec | MJPEG o raw RGB32 selezionabile da menu |

---

## 2. Testing Avanzato Driver

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Stress test automatico | 20/100 cicli start/stop con opzione resolution change |
| [x] | Test di latenza | Min/Max/Avg in ms tramite timestamp consumer |
| [x] | Confronto formati | Benchmark FPS/qualità per formato (`_RunFormatBenchmark`) |
| [x] | Report diagnostico | Export testuale con listusb, syslog, format negotiation log |
| [x] | Test memoria | 60s test con monitoraggio allocazioni |
| [x] | Heatmap drop frame | `DropFrameGraphView`: timeline FPS + drop markers |
| [x] | Cycle test | Test connect/disconnect hot-plug con timing variabili (100ms-2s) |
| [x] | Export risultati | CSV e JSON con timestamp per analisi cross-session |

---

## 3. Interfaccia Utente

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Tema scuro | `ui_color()` + `tint_color()` ovunque |
| [x] | Finestra floating | Preview always-on-top con `PreviewReplicant` |
| [x] | Zoom digitale | Mouse wheel 1x-8x + pan con drag |
| [x] | Griglia overlay | Rule of thirds + center crosshair toggleable |
| [x] | Histogram | RGB overlay real-time (Cmd+H) |
| [x] | Confronto A/B | Split view reference vs live con divider giallo |
| [x] | Fullscreen mode | Tasto Enter, escape per uscire |
| [x] | Personalizzazione layout | Pannelli toggle + reset, salvato nelle settings |
| [x] | Click-to-freeze / drag-to-save | Drag immagine direttamente fuori finestra |

---

## 4. Formati e Codec

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Decodifica MJPEG | Via libjpeg-turbo, decode multi-frame |
| [~] | Supporto H.264 | NAL unit detection + placeholder (decoder non disponibile su Haiku) |
| [x] | Conversione NV12 | YUV 4:2:0 semi-planar |
| [x] | Conversione NV21 | Variante con UV invertito |
| [x] | Conversione UYVY | Auto-detection + SSE2-optimized |
| [x] | Conversione YUV422/420 | SSE2-optimized (8 pixel per iterazione) |
| [x] | Conversione B_GRAY8 | Per webcam monocromatiche UVC |
| [x] | Raw frame export | Dump diretto buffer per debug driver |
| [x] | Info formato dettagliato | Bits/pixel, stride, planes nel pannello |

---

## 5. Controlli Webcam

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Preset salvataggio | File .bcpreset in `~/config/settings/BubiCam/presets/` |
| [x] | Auto-exposure lock | Toggle Lock AE |
| [x] | White balance lock | Toggle Lock AWB |
| [-] | Face detection | Non disponibile su Haiku |
| [x] | PTZ controls | Supportato via `BParameterWeb` generico |
| [x] | Reset to defaults | Ripristina valori iniziali dal driver |
| [x] | Controlli rapidi | Brightness/contrast primi nel pannello |

---

## 6. Integrazione Sistema Haiku

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Replicant Deskbar | `DeskbarReplicant` con LED indicator |
| [x] | Replicant Desktop | `PreviewReplicant` con live preview embeddabile |
| [x] | Scripting hey | `BHandler` scripting con `GetSupportedSuites()` |
| [x] | Notifiche sistema | `BNotification` per eventi (crash, disconnect, error) |
| [x] | Hotplug detection | Media node watcher per USB connect/disconnect |
| [x] | Preferenze persistenti | `~/config/settings/BubiCam/settings` salvate al volo |
| [x] | File type handling | MIME types per .bcpreset, .bcreport |
| [x] | Localizzazione | EN, IT, DE, ZH, JA via `BCatalog` |

---

## 7. Network e Streaming

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | HTTP streaming | `StreamServer`: MJPEG over HTTP per browser |
| [x] | Virtual webcam | `VirtualProducer` esposto come `BMediaAddOn` |
| [x] | Snapshot HTTP | Endpoint `/snapshot.jpg` |
| [x] | MCP server | Server JSON-RPC su porta 9847 per Claude Code |
| [ ] | Remote control via REST | Estensione futura del MCP server |
| [ ] | RTSP server | Fuori scope (troppo complesso) |

---

## 8. Funzionalità per Sviluppatori

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | API pubblica | `libwebcam.so` (header in `lib/libwebcam/include/`) |
| [x] | Plugin system | `VideoFilter` con built-in effects |
| [x] | Log viewer avanzato | `SyslogView` con regex filter, colori, ricerca |
| [x] | USB packet viewer | `USBPacketView` con tabs (descriptors + hex dump) |
| [x] | Frame inspector | Pixel inspector con valori RGB e metadata |
| [x] | Export per debug | Dump completo stato app (`_ExportDebugState`, salva in `~/`) |
| [x] | Command line mode | `bubicam --headless` per server-only |

---

## 9. Stabilità e Robustezza

| Stato | Feature | Note |
|:-----:|---------|------|
| [x] | Emergency exit watchdog | Thread indipendente in `BubiCamApp`, forza `_exit()` dopo 15s |
| [x] | Shutdown watchdog | Watchdog interno in `MainWindow::QuitRequested` |
| [x] | Lock ordering | `fCaptureLock`, `fWebcamLock`, `fTargetLock` ordinati per evitare ABBA |
| [x] | Race condition StartCapture/StopCapture | Mutual exclusion via `fCaptureLock` |
| [x] | StopNode timeout | Wrapper con timeout 3s + heap-allocated state |
| [x] | Force stop async | Background thread per StopCapture su driver frozen |
| [x] | Consumer thread join | `wait_for_thread(ControlThread())` prima di delete |
| [x] | PostMessage fuori lock | `AudioConsumer` e `VideoConsumer` rilasciano lock prima |
| [x] | Buffer truncation guard | Validazione `srcSize` prima di conversione YUV |
| [x] | MCP thread safety | `BLocker` su `fWebcamDevice` |
| [x] | EnumerateDevices safe | Stop preview + clear pointers prima del refresh |
| [x] | `Connected()` returns error | Su `CreateBuffers` failure (no producer stall) |

---

## Priorità Release

### v2.0 (rilasciata)
- [x] Tutte le feature core di video preview, recording, testing
- [x] Tema scuro, zoom, histogram, A/B compare, fullscreen
- [x] MCP server, replicant, streaming HTTP
- [x] Stability hardening completo

### v2.1 (future)
- [ ] H.264 decode (in attesa di port FFmpeg su Haiku)
- [ ] Remote control via REST API
- [ ] Snapshot scheduler con cron-style

### Post v2.1 (potenziali)
- [ ] RTSP server (se necessario)
- [ ] Web UI per controllo remoto
- [ ] Plugin filter scriptabili (Python/Lua)

---

## Note Tecniche

### Dipendenze
- Haiku R1/beta5 o successive
- libjpeg-turbo (per MJPEG)
- Haiku Media Kit + USB Kit + Translation Kit

### Architetture supportate
- x86_64 (primaria, testata)
- x86_gcc2 (best effort)

### Webcam testate
- Logitech C920, C270
- Generic UVC class devices
- Webcam monocromatiche UVC

### Driver target
- `usb_webcam` (UVC)
- `aukey_webcam`

---

## Changelog Documento

| Data | Versione | Modifiche |
|------|----------|-----------|
| 2024-12-16 | 0.1 | Creazione iniziale roadmap |
| 2024-12-16 | 0.2 | Implementato capitolo 2 (Testing Avanzato Driver) |
| 2024-12-16 | 0.3 | Implementato USB Packet Viewer (cap. 8) |
| 2026-05-23 | 0.4 | Recording AVI, MJPEG, NV12/NV21, zoom/histogram/A-B compare, hotplug, settings, cycle test, tema, click-to-freeze, icona app |
| 2026-05-23 | 0.5 | Cap. 5 completato (preset, lock AE/AWB, PTZ, reset, controlli rapidi) |
| 2026-06-24 | 1.0 | Sweep completo: cap. 6 (replicant, scripting, notifiche, localizzazione, MIME), cap. 7 (streaming HTTP, virtual webcam, MCP server), cap. 8 (libwebcam.so, plugin filter, frame inspector, CLI), nuovo cap. 9 (stability hardening) |
| 2026-07-01 | 1.1 | Refactoring struttura sorgenti (split `utils`/`services`, icone in `resources/icons/`); safety fix frame delivery (copia di proprietà del bitmap, no data race/UAF) e save panel screenshot/export; contratto ownership frame allineato nella doc di libwebcam |

---

*Documento del progetto BubiCam - Webcam Driver Tester per Haiku OS*
