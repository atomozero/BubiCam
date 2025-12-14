# BubiCam - Task di Miglioramento

Questo documento traccia i task di miglioramento del codice identificati durante l'analisi.

## Stato dei Task

| # | Task | Priorità | Stato | File Coinvolti |
|---|------|----------|-------|----------------|
| 1 | Aggiungere BLocker per fTarget in VideoConsumer/AudioConsumer | Alta | COMPLETATO | VideoConsumer.h/cpp, AudioConsumer.h/cpp |
| 2 | Fix race condition in WebcamDevice::StopCapture() | Alta | COMPLETATO | WebcamDevice.h/cpp |
| 3 | Aggiungere lock per fCurrentWebcam in MainWindow | Alta | COMPLETATO | MainWindow.h/cpp |
| 4 | Aggiungere controllo allocazione per tutte le new BBitmap | Alta | COMPLETATO | MainWindow.cpp, VideoPreviewView.cpp, IconUtils.cpp |
| 5 | Validare lunghezza descrittori USB prima di accedere | Media | Da fare | USBVideoParser.cpp |
| 6 | Verificare dimensioni prima di memcpy in MainWindow | Media | Da fare | MainWindow.cpp |
| 7 | Convertire BList a BObjectList in USBVideoParser | Media | Da fare | USBVideoParser.h/cpp |
| 8 | Usare atomic_int invece di volatile bool in MCPServer | Media | Da fare | MCPServer.h/cpp |
| 9 | Documentare magic numbers con commenti | Bassa | Da fare | Vari file |
| 10 | Refactoring WebcamDevice per ridurre responsabilita | Bassa | Da fare | WebcamDevice.h/cpp |
| 11 | Standardizzare gestione errori (status_t vs BAlert) | Bassa | Da fare | Vari file |

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
USBVideoParser non valida la lunghezza dei descrittori prima di accedere ai campi.

**Stato:** Da fare

---

### Task 6: Verifica dimensioni memcpy (PRIORITA MEDIA)

**Problema:**
`memcpy` in MainWindow non verifica che le dimensioni siano compatibili.

**Stato:** Da fare

---

### Task 7: BList -> BObjectList (PRIORITA MEDIA)

**Problema:**
USBVideoParser usa BList con cancellazione manuale, error-prone.

**Stato:** Da fare

---

### Task 8: atomic_int in MCPServer (PRIORITA MEDIA)

**Problema:**
`volatile bool fRunning` non garantisce atomicita.

**Stato:** Da fare

---

### Task 9: Documentare magic numbers (PRIORITA BASSA)

**Problema:**
Numeri come `NUM_BUFFERS 3`, `kMaxLatency = 50000` non documentati.

**Stato:** Da fare

---

### Task 10: Refactoring WebcamDevice (PRIORITA BASSA)

**Problema:**
WebcamDevice ha troppe responsabilita (32 metodi, 40+ variabili).

**Stato:** Da fare

---

### Task 11: Standardizzare gestione errori (PRIORITA BASSA)

**Problema:**
Gestione errori inconsistente tra status_t, BAlert e stderr.

**Stato:** Da fare

---

## Changelog

- **2024-12-14**: Task 4 completato - Aggiunto controllo allocazione BBitmap
- **2024-12-14**: Task 3 completato - Aggiunto BLocker per fCurrentWebcam
- **2024-12-14**: Task 2 completato - Fix race condition in StopCapture
- **2024-12-14**: Task 1 completato - Aggiunto BLocker per fTarget
- **2024-12-14**: Documento creato
