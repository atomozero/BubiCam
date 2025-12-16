# BubiCam 2.0 - Roadmap

Documento di pianificazione per le funzionalità della versione 2.0 di BubiCam.

**Versione attuale:** 1.0
**Ultimo aggiornamento:** 2024-12-16
**Stato:** In pianificazione

---

## Legenda Stati

- [ ] Non iniziato
- [~] In sviluppo
- [x] Completato
- [!] Bloccato / Problema
- [-] Cancellato / Rimandato

---

## 1. Registrazione Video/Audio

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **Registrazione video** | Salvataggio stream video in file (AVI, MKV) | Richiede MediaEncoder |
| [ ] | **Registrazione audio** | Cattura audio dal microfono integrato webcam | Sincronizzazione A/V |
| [ ] | **Time-lapse** | Scatto automatico a intervalli configurabili | Intervallo 1s - 1h |
| [ ] | **Buffer circolare** | "Salva ultimi N secondi" per catturare eventi già accaduti | RAM buffer 10-60s |
| [ ] | **Selezione codec** | Scelta codec video (raw, MJPEG, etc.) | Dipende da MediaKit |

### Note implementazione
```
- Usare BMediaFile + BMediaTrack per scrittura
- Formato container: AVI (più compatibile) o MKV
- Considerare compressione MJPEG per ridurre dimensione file
```

---

## 2. Testing Avanzato Driver

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [x] | **Stress test automatico** | Start/stop ripetuto, cambio risoluzione ciclico | 20/100 cicli, opzione res change |
| [x] | **Test di latenza** | Misura tempo tra cattura e visualizzazione | Min/Max/Avg in ms |
| [x] | **Confronto formati** | Benchmark qualità/performance per ogni formato | Test 3s per formato |
| [x] | **Report diagnostico** | Export completo per bug report Haiku | Include listusb, syslog |
| [x] | **Test memoria** | Monitoraggio allocazioni durante uso prolungato | 60s test, rileva leak |
| [x] | **Heatmap drop frame** | Grafico temporale dei frame persi | Grafico FPS + drop markers |
| [ ] | **Cycle test** | Test ciclico connect/disconnect simulato | Per stabilità driver |

### Note implementazione
```
- Stress test: loop con snooze configurabile
- Latenza: timestamp in VideoConsumer vs visualizzazione
- Report: eseguire listusb -v, listdev, cat /var/log/syslog | grep usb
```

---

## 3. Interfaccia Utente

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **Tema scuro** | Supporto dark mode nativo Haiku | Rispetta ui_color() |
| [ ] | **Finestra floating** | Preview always-on-top ridimensionabile | B_FLOATING_WINDOW |
| [ ] | **Zoom digitale** | Ingrandimento area specifica del video | Mouse scroll + drag |
| [ ] | **Griglia overlay** | Allineamento (rule of thirds, center cross) | Toggle on/off |
| [ ] | **Histogram** | Distribuzione luminosità in tempo reale | RGB + luminanza |
| [ ] | **Confronto A/B** | Due webcam affiancate | Split view |
| [ ] | **Fullscreen mode** | Preview a schermo intero | Tasto F o doppio click |
| [ ] | **Personalizzazione layout** | Pannelli trascinabili/nascondibili | Salva in settings |

### Note implementazione
```
- Tema: usare B_PANEL_BACKGROUND_COLOR e simili ovunque
- Floating: nuova classe MiniPreviewWindow
- Zoom: BView::SetScale() o rendering manuale
- Histogram: calcolo su buffer RGB, draw con BView
```

---

## 4. Formati e Codec

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **Decodifica MJPEG** | Supporto webcam con output MJPEG | Usa TranslatorRoster |
| [ ] | **Supporto H.264** | Per webcam moderne | Richiede decoder |
| [ ] | **Conversione NV12** | Formato comune in webcam moderne | YUV 4:2:0 planar |
| [ ] | **Conversione NV21** | Variante Android-style | UV invertito |
| [ ] | **Conversione UYVY** | Alternativa a YUYV | Ordine byte diverso |
| [ ] | **Raw frame export** | Salvataggio frame singolo non processato | Per debug driver |
| [ ] | **Info formato dettagliato** | Mostra bits/pixel, stride, planes | Nel pannello info |

### Note implementazione
```
- MJPEG: BBitmap + BTranslatorRoster::Translate()
- NV12/NV21: conversione manuale Y plane + UV interleaved
- Raw export: dump diretto buffer con header custom
```

---

## 5. Controlli Webcam

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **Preset salvataggio** | Salva/carica configurazioni controlli | File .bcpreset |
| [ ] | **Auto-exposure lock** | Blocca esposizione corrente | Se supportato |
| [ ] | **White balance lock** | Blocca bilanciamento bianco | Se supportato |
| [ ] | **Face detection** | Evidenzia volti (se driver supporta) | Overlay rettangoli |
| [ ] | **PTZ controls** | Pan/Tilt/Zoom per webcam motorizzate | UI dedicata |
| [ ] | **Reset to defaults** | Ripristina valori fabbrica | Un click |
| [ ] | **Controlli rapidi** | Slider principali sempre visibili | Brightness, contrast |

### Note implementazione
```
- Preset: BMessage serializzato in file
- PTZ: comandi UVC specifici via ParameterWeb o ioctl
- Face detection: probabilmente non disponibile su Haiku
```

---

## 6. Integrazione Sistema Haiku

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **Replicant Deskbar** | Icona con stato e quick preview | BDragger |
| [ ] | **Scripting hey** | Controllo completo via comando hey | BHandler scripting |
| [ ] | **Notifiche sistema** | Alert per eventi (crash, disconnect) | BNotification |
| [ ] | **Hotplug detection** | Rilevamento automatico USB connect/disconnect | BUSBRoster watch |
| [ ] | **Preferenze persistenti** | Salvataggio impostazioni tra sessioni | ~/config/settings |
| [ ] | **File type handling** | Associazione .bcpreset, .bcreport | MIME types |
| [ ] | **Localizzazione** | Traduzioni italiano, inglese, tedesco | BCatalog |

### Note implementazione
```
- Replicant: classe derivata da BView con Archive/Instantiate
- hey scripting: implementare GetSupportedSuites(), ResolveSpecifier()
- Hotplug: sottoclasse BUSBRoster con DeviceAdded/DeviceRemoved
- Settings: ~/config/settings/BubiCam/settings
```

---

## 7. Network e Streaming

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **HTTP streaming** | Streaming MJPEG via browser locale | Porta configurabile |
| [ ] | **Virtual webcam** | Output come sorgente video per altre app | Media add-on |
| [ ] | **Remote control** | Controllo da altra macchina via rete | Protocollo custom o REST |
| [ ] | **RTSP server** | Streaming standard per client video | Complesso |
| [ ] | **Snapshot HTTP** | Singola immagine via URL | /snapshot.jpg |

### Note implementazione
```
- HTTP streaming: socket server + MJPEG boundary
- Virtual webcam: BMediaAddOn che espone BBufferProducer
- RTSP: probabilmente fuori scope, molto complesso
```

---

## 8. Funzionalità per Sviluppatori

| Stato | Feature | Descrizione | Note |
|:-----:|---------|-------------|------|
| [ ] | **API pubblica** | Libreria libbubicam.so per altre app | Header pubblici |
| [ ] | **Plugin system** | Filtri video custom (effetti, overlay) | BAddOn |
| [ ] | **Log viewer avanzato** | Filtri regex, colorazione, ricerca | Potenzia SyslogView |
| [ ] | **USB packet viewer** | Visualizzazione traffico USB raw | Richiede permessi root |
| [ ] | **Frame inspector** | Analisi dettagliata singolo frame | Pixel values, metadata |
| [ ] | **Export per debug** | Dump completo stato applicazione | Per bug report |
| [ ] | **Command line mode** | Esecuzione senza GUI | bubicam --capture file.avi |

### Note implementazione
```
- Plugin: directory ~/config/non-packaged/add-ons/BubiCam/filters
- USB viewer: usb_raw driver, richiede be_user group
- CLI: BApplication con ArgvReceived(), no window se --headless
```

---

## Priorità Release

### v2.0-alpha (Prima release)
- [ ] Registrazione video base
- [ ] Hotplug detection
- [ ] Preset salvataggio controlli
- [ ] Report diagnostico

### v2.0-beta
- [ ] Tema scuro
- [ ] MJPEG decode
- [ ] Stress test automatico
- [ ] Replicant Deskbar

### v2.0-final
- [ ] HTTP streaming
- [ ] Zoom digitale
- [ ] Histogram
- [ ] Localizzazione

### Post v2.0 (Future)
- [ ] Virtual webcam
- [ ] Plugin system
- [ ] H.264 support
- [ ] Command line mode

---

## Note Tecniche Generali

### Dipendenze potenziali
- FFmpeg port (per codec avanzati)
- libjpeg (per MJPEG, già in Haiku)
- Haiku private headers (per alcune funzionalità USB)

### Compatibilità
- Target: Haiku R1/beta5 e successive
- Architetture: x86_64 (primaria), x86_gcc2 (se possibile)

### Testing
- Webcam testate: Logitech C920, C270, generic UVC
- Driver target: usb_webcam (UVC), aukey_webcam

---

## Changelog Documento

| Data | Versione | Modifiche |
|------|----------|-----------|
| 2024-12-16 | 0.1 | Creazione iniziale roadmap |
| 2024-12-16 | 0.2 | Implementato capitolo 2 (Testing Avanzato Driver) |

---

*Documento generato per il progetto BubiCam - Webcam Driver Tester per Haiku OS*
