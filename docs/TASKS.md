# BubiCam - Task di Miglioramento

Questo documento traccia i task di miglioramento del codice identificati durante l'analisi.

## Stato dei Task

| # | Task | Priorità | Stato | File Coinvolti |
|---|------|----------|-------|----------------|
| 1 | Aggiungere BLocker per fTarget in VideoConsumer/AudioConsumer | Alta | COMPLETATO | VideoConsumer.h/cpp, AudioConsumer.h/cpp |
| 2 | Fix race condition in WebcamDevice::StopCapture() | Alta | COMPLETATO | WebcamDevice.h/cpp |
| 3 | Aggiungere lock per fCurrentWebcam in MainWindow | Alta | COMPLETATO | MainWindow.h/cpp |
| 4 | Aggiungere controllo allocazione per tutte le new BBitmap | Alta | COMPLETATO | MainWindow.cpp, VideoPreviewView.cpp, IconUtils.cpp |
| 5 | Validare lunghezza descrittori USB prima di accedere | Media | COMPLETATO | USBVideoParser.cpp |
| 6 | Verificare dimensioni prima di memcpy in MainWindow | Media | COMPLETATO | MainWindow.cpp, VideoPreviewView.cpp |
| 7 | Convertire BList a BObjectList in USBVideoParser | Media | COMPLETATO | USBVideoParser.h/cpp, DriverInfoView.cpp, WebcamDevice.cpp |
| 8 | Usare atomic_int invece di volatile bool in MCPServer | Media | COMPLETATO | MCPServer.h/cpp |
| 9 | Documentare magic numbers con commenti | Bassa | COMPLETATO | VideoConsumer.h/cpp, WebcamDevice.cpp |
| 10 | Refactoring WebcamDevice per ridurre responsabilita | Bassa | COMPLETATO | WebcamDevice.h/cpp |
| 11 | Standardizzare gestione errori (status_t vs BAlert) | Bassa | COMPLETATO | ErrorUtils.h + vari file |

---

## Dettagli Task

### Task 1: BLocker per fTarget (PRIORITA ALTA)

**Problema:**
In `VideoConsumer::_SendFrameToTarget()` e `AudioConsumer::_HandleBuffer()`, il puntatore `fTarget` viene controllato per NULL e poi usato, ma senza sincronizzazione. In ambiente multi-thread, `fTarget` potrebbe essere eliminato tra il check e l'uso.

```cpp
// RACE CONDITION - fTarget potrebbe diventare NULL dopo questo check
if (fTarget == NULL || bitmap == NULL)
    return;
fTarget->PostMessage(&msg);  // CRASH se fTarget eliminato qui
```

**Soluzione:**
- Aggiungere `BLocker fTargetLock` per proteggere l'accesso a `fTarget`
- Usare lock in tutti i metodi che accedono a `fTarget`
- Aggiungere metodo `SetTarget()` thread-safe

**Test di verifica:**
- Compilare e eseguire senza crash durante start/stop ripetuti
- Verificare che i frame continuino ad arrivare correttamente
- Eseguire: `./tests/test_task1_target_lock.sh`

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `VideoConsumer.h`: Aggiunto `#include <Locker.h>`, `mutable BLocker fTargetLock`, metodo `SetTarget()`
- `VideoConsumer.cpp`: Aggiunto `#include <Autolock.h>`, implementato `SetTarget()`, modificato `_SendFrameToTarget()` per usare `BAutolock`
- `AudioConsumer.h`: Aggiunto `#include <Locker.h>`, `mutable BLocker fTargetLock`, metodo `SetTarget()`
- `AudioConsumer.cpp`: Aggiunto `#include <Autolock.h>`, implementato `SetTarget()`, modificato `_HandleBuffer()` per usare `BAutolock`

---

### Task 2: Race condition StopCapture (PRIORITA ALTA)

**Problema:**
In `WebcamDevice::StopCapture()` c'e una finestra temporale dove i puntatori potrebbero essere invalidi durante lo shutdown. I metodi `FramesCaptured()`, `FramesDropped()`, `CurrentFPS()`, `GetCurrentFrame()` accedevano ai consumer pointers senza sincronizzazione.

**Soluzione:**
- Aggiungere `BLocker fCaptureLock` per proteggere l'accesso ai consumer pointers
- `StopCapture()` ora acquisisce il lock, chiama `SetTarget(NULL)` sui consumer, salva copie locali, cancella i member pointers, rilascia il lock, ed esegue il cleanup
- I metodi di accesso (`FramesCaptured()`, etc.) usano il lock

**Test di verifica:**
- Eseguire: `./tests/test_task2_stopcapture_race.sh`
- Verificare nessun crash durante cicli rapidi start/stop

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `WebcamDevice.h`: Aggiunto `#include <Locker.h>`, `mutable BLocker fCaptureLock`
- `WebcamDevice.cpp`: Aggiunto `#include <Autolock.h>`, modificati `FramesCaptured()`, `FramesDropped()`, `CurrentFPS()`, `GetCurrentFrame()` per usare il lock, riscritta `StopCapture()` con lock e chiamata a `SetTarget(NULL)`

---

### Task 3: Lock per fCurrentWebcam (PRIORITA ALTA)

**Problema:**
`fCurrentWebcam` in MainWindow viene acceduto da piu' message handler senza protezione. In particolare, `MSG_FRAME_RECEIVED` (che arriva dal Media Kit) poteva accedere a `fCurrentWebcam` mentre altri handler lo modificavano.

**Soluzione:**
- Aggiungere `BLocker fWebcamLock` per proteggere l'accesso a `fCurrentWebcam`
- `_SelectWebcam()` usa il lock quando modifica `fCurrentWebcam`
- `MSG_FRAME_RECEIVED` usa il lock quando accede alle statistiche
- `QuitRequested()` usa il lock quando modifica `fCurrentWebcam`

**Test di verifica:**
- Eseguire: `./tests/test_task3_webcam_lock.sh`
- Verificare nessun crash durante cicli di selezione webcam

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `MainWindow.h`: Aggiunto `#include <Locker.h>`, `mutable BLocker fWebcamLock`
- `MainWindow.cpp`: Aggiunto `#include <Autolock.h>`, modificati `_SelectWebcam()`, `MSG_FRAME_RECEIVED`, `QuitRequested()` per usare il lock

---

### Task 4: Controllo allocazione BBitmap (PRIORITA ALTA)

**Problema:**
`new BBitmap()` non veniva controllato per fallimento allocazione. In Haiku, BBitmap non lancia eccezioni ma crea un oggetto invalido (`IsValid()` ritorna false). Usare un bitmap invalido causava crash o comportamenti undefined.

**Soluzione:**
- Controllare `IsValid()` dopo ogni `new BBitmap()`
- Se invalido, deallocare e gestire gracefully (return NULL o skip operation)
- Nota: `VideoConsumer.cpp` aveva gia' i controlli corretti

**Test di verifica:**
- Eseguire: `./tests/test_task4_bitmap_alloc.sh`
- Verificare nessun crash durante avvio e uso prolungato

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `MainWindow.cpp`: Controllo `IsValid()` dopo allocazione `fLastFrame`, skip memcpy se invalido
- `VideoPreviewView.cpp`: Controllo `IsValid()` dopo allocazione `fCurrentFrame`, return early se invalido
- `IconUtils.cpp`: Controllo `IsValid()` in `CreateRefreshIcon()`, `CreateStartIcon()`, `CreateStopIcon()`, `CreateScreenshotIcon()`, return NULL se invalido

---

### Task 5: Validazione descrittori USB (PRIORITA MEDIA)

**Problema:**
USBVideoParser non validava la lunghezza dei descrittori USB prima di accedere ai campi interni. Descrittori malformati o corrotti potevano causare accessi fuori dai limiti del buffer, crash durante il parsing, o comportamenti undefined.

**Soluzione:**
- Definita costante `kBufferSize = 1024` per evitare magic numbers
- Aggiunto controllo lunghezza descrittore (min 2 byte, max kBufferSize)
- Aggiunto sanity check per dimensioni frame (0 < width/height <= 8192)
- Aggiunto bounds checking per parsing frame intervals
- Aggiunto controllo allocazione USBVideoFrame
- Aggiunti messaggi diagnostici WARNING per descrittori invalidi

**Test di verifica:**
- Eseguire: `./tests/test_task5_usb_validation.sh`
- Verificare nessun crash durante enumerazione USB

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `USBVideoParser.cpp`: Aggiunta validazione lunghezza in `_ParseVideoControl()` e `_ParseVideoStreaming()`, bounds checking per frame intervals, sanity check dimensioni frame, controllo allocazione USBVideoFrame

---

### Task 6: Verifica dimensioni memcpy (PRIORITA MEDIA)

**Problema:**
`memcpy` in MainWindow e VideoPreviewView copiava dati bitmap senza verificare che il buffer di destinazione fosse abbastanza grande. In MainWindow, inoltre, il check per riallocare il bitmap controllava solo le bounds ma non il color space, potendo causare mismatch di BitsLength.

**Soluzione:**
- MainWindow.cpp: Aggiunto check `ColorSpace()` nella condizione di riallocazione
- MainWindow.cpp: Aggiunto check `BitsLength() >= bitmap->BitsLength()` prima di memcpy
- VideoPreviewView.cpp: Aggiunto check `BitsLength() >= bitmap->BitsLength()` prima di memcpy

**Test di verifica:**
- Eseguire: `./tests/test_task6_memcpy_safety.sh`
- Verificare nessun crash durante ricezione frame e screenshot

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `MainWindow.cpp`: Aggiunto check ColorSpace nella riallocazione fLastFrame, aggiunto check BitsLength prima di memcpy
- `VideoPreviewView.cpp`: Aggiunto check BitsLength prima di memcpy in SetFrame()

---

### Task 7: BList -> BObjectList (PRIORITA MEDIA)

**Problema:**
USBVideoParser usava BList con void* pointers che richiedevano casting manuali error-prone e deallocazione manuale nei distruttori. Nessun type-safety a compile time.

**Soluzione:**
- Convertito `BList` a `BObjectList<T, true>` (owning) per gestione automatica memoria
- `USBVideoInfo::formats` -> `BObjectList<USBVideoFormat, true>`
- `USBVideoFormat::frames` -> `BObjectList<USBVideoFrame, true>`
- `USBVideoFrame::frameRates` -> `BObjectList<FrameRate, true>` (nuovo wrapper struct)
- Rimossi tutti i cast manuali nei file che accedevano alle liste
- Rimossi i distruttori con loop di delete (ora automatici)

**Test di verifica:**
- Eseguire: `./tests/test_task7_bobjectlist.sh`
- Verificare nessun crash e nessun memory leak

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `USBVideoParser.h`: Sostituito `#include <List.h>` con `#include <ObjectList.h>`, aggiunto struct `FrameRate`, convertite le 3 liste a `BObjectList<T, true>`, rimossi distruttori manuali
- `USBVideoParser.cpp`: Rimossi cast `(void*)` da AddItem(), usato `new FrameRate(value)` invece di `new float`
- `DriverInfoView.cpp`: Rimossi cast manuali, usato `fps->value` per accedere al frame rate
- `WebcamDevice.cpp`: Rimossi cast manuali da ItemAt()

---

### Task 8: atomic_int in MCPServer (PRIORITA MEDIA)

**Problema:**
`volatile bool fRunning` in MCPServer non garantisce atomicita. `volatile` previene solo le ottimizzazioni del compilatore che cacherebbero il valore, ma NON garantisce operazioni atomiche o memory barriers. Su sistemi multi-core, un thread potrebbe non vedere le modifiche fatte da un altro thread.

**Soluzione:**
- Convertito `volatile bool fRunning` a `std::atomic<bool> fRunning`
- Convertito i contatori `int32` a `std::atomic<int32>` per consistenza
- Sostituito chiamate `atomic_add(&var, n)` con operatori `++`/`--` di std::atomic
- Aggiunto `#include <atomic>` al header

`std::atomic` garantisce:
- Operazioni atomiche (no torn reads/writes)
- Memory barriers per corretta visibilita tra thread
- Lock-free su la maggior parte delle architetture

**Test di verifica:**
- Eseguire: `./tests/test_task8_atomic.sh`
- Verificare avvii/chiusure rapidi senza crash

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `MCPServer.h`: Aggiunto `#include <atomic>`, convertito `volatile bool fRunning` a `std::atomic<bool>`, convertito contatori a `std::atomic<int32>`
- `MCPServer.cpp`: Sostituito `atomic_add()` con `++`/`--` operators

---

### Task 9: Documentare magic numbers (PRIORITA BASSA)

**Problema:**
Il codice conteneva numeri "magici" non documentati che rendevano difficile capire il rationale delle scelte implementative. Esempi: `NUM_BUFFERS 3`, valori di latenza `50000`, risoluzioni `320x240`, delay `100000`.

**Soluzione:**
- Aggiunti commenti dettagliati che spiegano:
  1. Cosa rappresenta ogni costante
  2. Perche' e' stato scelto quel valore specifico
  3. Quali conseguenze ha cambiare il valore
- Convertiti numeri letterali in costanti con nome significativo
- Costanti definite:
  - `NUM_BUFFERS`: pattern triple-buffering per webcam
  - `kMaxLatency`: 50ms bilanciamento latenza/stabilita
  - `kFallbackWidth/Height`: 320x240 QVGA universalmente supportato
  - `kMediaStartDelay`: 100ms per inizializzazione driver
  - `kPostSeekDelay`: 50ms stabilizzazione post-seek

**Test di verifica:**
- Eseguire: `./tests/test_task9_magic_numbers.sh`
- Verificare che costanti siano definite e usate

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `VideoConsumer.h`: Documentato `NUM_BUFFERS` con spiegazione triple-buffering
- `VideoConsumer.cpp`: Aggiunto `kMaxLatency`, `kFallbackWidth`, `kFallbackHeight` con commenti, usati nel codice
- `WebcamDevice.cpp`: Aggiunto `kMediaStartDelay`, `kPostSeekDelay`, `kFallbackWidth`, `kFallbackHeight` con commenti, sostituiti numeri letterali

---

### Task 10: Refactoring WebcamDevice (PRIORITA BASSA)

**Problema:**
WebcamDevice aveva ~40 variabili membro sparse senza raggruppamento logico:
- 9 variabili per informazioni USB (vendorID, productID, vendorName, etc.)
- 3 variabili per informazioni driver (driverName, driverPath, driverVersion)

**Soluzione:**
- Creata struct `USBDeviceInfo` che raggruppa tutte le 9 variabili USB:
  - vendorID, productID, vendorName, productName
  - serialNumber, usbVersion
  - deviceClass, deviceSubclass, deviceProtocol
- Creata struct `DriverInfo` che raggruppa tutte le 3 variabili driver:
  - name, path, version
- Sostituite le variabili membro individuali con le nuove struct
- API pubblica invariata (getter delegano alle struct)
- Aggiunti nuovi metodi `GetUSBInfo()` e `GetDriverInfo()` per accesso diretto

**Benefici:**
- Riduzione variabili membro individuali (da ~40 a ~30)
- Codice piu' organizzato e leggibile
- Possibilita' di passare info USB/driver come unita' singola
- Nessun breaking change per i consumatori esistenti

**Test di verifica:**
- Eseguire: `./tests/test_task10_refactoring.sh`
- Verificare struct definite e usate correttamente

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `WebcamDevice.h`: Aggiunte struct `USBDeviceInfo` e `DriverInfo`, sostituiti membri individuali, aggiunti getter `GetUSBInfo()` e `GetDriverInfo()`
- `WebcamDevice.cpp`: Aggiornati costruttori e metodi `_GatherUSBInfo()`, `_GatherDriverInfo()`, `ParseUSBDescriptors()` per usare le nuove struct

---

### Task 11: Standardizzare gestione errori (PRIORITA BASSA)

**Problema:**
Gestione errori inconsistente nel codebase:
- Alcuni errori mostrati con BAlert (visibili all'utente)
- Altri errori solo con fprintf(stderr) (invisibili all'utente)
- Messaggi di errore con formati diversi
- Nessun sistema centralizzato per logging

**Soluzione:**
Creato `ErrorUtils.h` che fornisce un sistema centralizzato di logging:

**Macro di logging (4 livelli di severita'):**
- `LOG_DEBUG(fmt, ...)` - Informazioni di debug
- `LOG_INFO(fmt, ...)` - Informazioni generali
- `LOG_WARNING(fmt, ...)` - Avvertimenti non critici
- `LOG_ERROR(fmt, ...)` - Errori critici

**Helper per alert utente:**
- `ShowErrorAlert(title, message)` - Alert errore (icona stop)
- `ShowWarningAlert(title, message)` - Alert avviso (icona warning)
- `ShowConfirmationAlert(title, message, cancel, confirm)` - Conferma con 2 pulsanti
- `ShowErrorAlertWithStatus(title, message, status)` - Alert con codice status_t

**Macro combinate:**
- `LOG_AND_SHOW_ERROR(title, fmt, ...)` - Log + alert in un passo
- `CHECK_STATUS(status, fmt, ...)` - Log errore e return se status != B_OK
- `CHECK_STATUS_LOG(status, fmt, ...)` - Log errore senza return

**Formato consistente:**
```
[MODULE] LEVEL: message
```
Ogni file definisce LOG_MODULE prima di includere ErrorUtils.h.

**Test di verifica:**
- Eseguire: `./tests/test_task11_error_handling.sh`
- Verificare macro e helper presenti, file aggiornati

**Stato:** COMPLETATO
**Completato:** 2024-12-14

**Modifiche apportate:**
- `src/utils/ErrorUtils.h`: Nuovo file con macro e helper
- `src/webcam/VideoConsumer.cpp`: Aggiunto LOG_MODULE + ErrorUtils, macro legacy delegano
- `src/webcam/AudioConsumer.cpp`: Aggiunto LOG_MODULE + ErrorUtils
- `src/webcam/WebcamDevice.cpp`: Aggiunto LOG_MODULE + ErrorUtils
- `src/MainWindow.cpp`: Aggiunto LOG_MODULE + ErrorUtils
- `src/mcp/MCPServer.cpp`: Aggiunto LOG_MODULE + ErrorUtils
- `src/utils/ExportUtils.cpp`: Aggiunto LOG_MODULE + ErrorUtils

---

## Changelog

- **2024-12-14**: Task 11 completato - Standardizzato gestione errori con ErrorUtils.h
- **2024-12-14**: Task 10 completato - Refactoring WebcamDevice con struct USBDeviceInfo e DriverInfo
- **2024-12-14**: Task 9 completato - Documentati magic numbers con costanti e commenti
- **2024-12-14**: Task 8 completato - Convertito volatile a std::atomic in MCPServer
- **2024-12-14**: Task 7 completato - Convertito BList a BObjectList type-safe
- **2024-12-14**: Task 6 completato - Aggiunto controllo dimensioni memcpy
- **2024-12-14**: Task 5 completato - Aggiunta validazione descrittori USB
- **2024-12-14**: Task 4 completato - Aggiunto controllo allocazione BBitmap
- **2024-12-14**: Task 3 completato - Aggiunto BLocker per fCurrentWebcam
- **2024-12-14**: Task 2 completato - Fix race condition in StopCapture
- **2024-12-14**: Task 1 completato - Aggiunto BLocker per fTarget
- **2024-12-14**: Documento creato
