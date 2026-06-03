# libbubicapture

Una piccola libreria di cattura per Haiku: elenca le webcam USB tramite il Media
Kit e ti consegna i frame video già decodificati (e i livelli audio), senza il
boilerplate del Media Kit.

È il componente `src/webcam/` di BubiCam, impacchettato perché altre
applicazioni possano riusarlo. BubiCam ne è un consumatore; la tua app può
essere un altro.

> Stato: questo documento descrive il contratto pubblico del componente di
> cattura. Il codice è in `src/webcam/`. Una volta estratto come libreria a sé,
> mantiene le stesse classi e lo stesso contratto descritto qui.

---

## Cosa fa per te

- Trova le webcam registrate nel Media Kit (`WebcamRoster`).
- Si connette a un device e avvia la pipeline di cattura (`WebcamDevice`).
- Converte ogni formato in ingresso (MJPEG, YUYV, I420, NV12, NV21, UYVY, RGB) in
  un unico formato pixel pronto all'uso e posta ogni frame al tuo `BLooper`.
- Riporta le statistiche di cattura (FPS, frame catturati, frame persi).
- Riporta i livelli di picco audio (sinistro/destro) per i VU meter.

Cosa **non** fa: encoding, registrazione, streaming di rete o UI. Quelli sono
compito dell'applicazione. La libreria ti dà solo frame puliti.

---

## Dipendenze

Solo librerie di sistema Haiku — nessun pacchetto di terze parti:

```
be media tracker translation device shared jpeg
```

`media` esegue la cattura, `jpeg` (libjpeg-turbo) decodifica le webcam MJPEG.

---

## Il modello mentale (leggi prima questo)

La libreria è **push-based** e costruita sul modello looper/messaggi di Haiku.
Non interroghi tu i frame: dai al device un `BLooper` e la libreria **gli posta
un `BMessage` per ogni frame**, da un thread del Media Kit.

```
  webcam (producer del Media Kit)
        │  buffer grezzi
        ▼
  WebcamDevice  ──►  VideoConsumer interno  ──►  conversione di formato
                                                        │
                                  BMessage('frcv') + puntatore "bitmap"
                                                        ▼
                                            IL TUO BLooper::MessageReceived()
```

Tre fatti che ne derivano, e che devi rispettare:

1. **Il tuo handler gira sul thread del looper, non su un thread di cattura.**
   Valgono le regole standard di Haiku: blocca le finestre prima di toccare le
   view, tieni l'handler breve.
2. **Il `BBitmap` del frame è di proprietà della libreria ed è valido solo dentro
   quel messaggio.** Se ti servono i pixel dopo il ritorno, **copiali**. Non fare
   `delete` del bitmap e non conservare il puntatore.
3. **I frame vengono scartati, non accodati, se rimani indietro.** La pipeline usa
   3 buffer; un handler lento significa frame persi (vedi `FramesDropped()`), mai
   crescita illimitata della memoria.

---

## Formato dei frame

Ogni frame consegnato è un `BBitmap` in **`B_RGB32`** (ordine byte BGRA in
memoria, 4 byte per pixel). Un solo formato, sempre — non devi mai ramificare sul
codec in ingresso. Le dimensioni le prendi da `bitmap->Bounds()`, i pixel da
`bitmap->Bits()` con `bitmap->BytesPerRow()`.

---

## Avvio rapido

Un capturer minimale senza interfaccia: enumera, avvia il primo device, conta i
frame.

```cpp
#include <Application.h>
#include <Looper.h>
#include <Bitmap.h>
#include "WebcamRoster.h"
#include "WebcamDevice.h"

// La costante del messaggio frame che la libreria posta (vedi "Contratto del
// messaggio frame").
static const uint32 kFrameReceived = 'frcv';

class CaptureLooper : public BLooper {
public:
    CaptureLooper() : BLooper("capture") {}

    void MessageReceived(BMessage* msg) override {
        if (msg->what == kFrameReceived) {
            BBitmap* frame = NULL;
            if (msg->FindPointer("bitmap", (void**)&frame) == B_OK && frame) {
                // 'frame' è valido SOLO qui. Copialo se ti serve dopo.
                fCount++;
            }
            return;
        }
        BLooper::MessageReceived(msg);
    }

    int32 fCount = 0;
};

int main() {
    BApplication app("application/x-vnd.example-capture");

    WebcamRoster roster;
    roster.EnumerateDevices();
    if (roster.CountDevices() == 0)
        return 1;

    WebcamDevice* dev = roster.DeviceAt(0);

    CaptureLooper* looper = new CaptureLooper();
    looper->Run();

    dev->StartCapture(looper);   // i frame ora scorrono verso il looper
    app.Run();                   // ... fai il tuo lavoro ...
    dev->StopCapture();
    return 0;
}
```

Questa è tutta la superficie di integrazione: enumera, `StartCapture(looper)`,
leggi i frame in `MessageReceived`, `StopCapture()`.

---

## Riferimento API

### `WebcamRoster` — trovare i device

`WebcamRoster` è un `BHandler`. L'enumerazione funziona da sola; le notifiche di
hot-plug richiedono di aggiungerlo a un `BLooper` in esecuzione.

| Metodo | Scopo |
|---|---|
| `status_t EnumerateDevices()` | Scansiona il Media Kit per le webcam. Chiamalo all'avvio, o dopo un evento di hot-plug. |
| `int32 CountDevices()` | Numero di webcam trovate. |
| `WebcamDevice* DeviceAt(int32 i)` | Device per indice. Di proprietà del roster — non fare delete. |
| `WebcamDevice* DeviceByName(const char* name)` | Device per nome, oppure `NULL`. |
| `status_t StartWatching()` / `void StopWatching()` | Abilita/disabilita le notifiche di hot-plug. |

Quando i device cambiano, il roster posta `MSG_DEVICES_CHANGED` (`'dvch'`) al
looper a cui è collegato. Ri-esegui `EnumerateDevices()` e aggiorna la tua lista.

### `WebcamDevice` — catturare da una webcam

Controllo della cattura:

| Metodo | Scopo |
|---|---|
| `status_t StartCapture(BLooper* target, uint32 frameMessage = MSG_WEBCAM_FRAME, uint32 audioLevelMessage = MSG_WEBCAM_AUDIO_LEVEL)` | Connette la pipeline e inizia a postare i frame a `target`. Passa i tuoi `what` per integrarti con un protocollo di messaggi tuo; i default restano `'frcv'`/`'audl'`. |
| `void StopCapture()` | Ferma e disconnette. Sicuro da chiamare da qualsiasi thread. |
| `bool IsCapturing()` | Se la cattura è in corso. |

Per catturare i campioni audio (non solo i livelli) — per registrazione o
encoding — passa un `AudioSink` all'`AudioConsumer` del device:
`device->GetAudioConsumer()->SetAudioSink(mioSink)`. Il sink riceve il PCM
grezzo direttamente dal thread audio (vedi `AudioSink.h`).

Selezione del formato (chiamare **prima** di `StartCapture`):

| Metodo | Scopo |
|---|---|
| `const BObjectList<VideoFormat>& SupportedFormats()` | Risoluzioni/frame rate dichiarati dal device. |
| `void SetRequestedFormat(const VideoFormat&)` | Richiede una risoluzione/rate specifica. Il driver può negoziare qualcosa di vicino. |
| `VideoFormat CurrentFormat()` | Il formato negoziato. Verificalo dopo i primi frame. |

Sorgente audio (chiamare **prima** di `StartCapture`):

| Metodo | Scopo |
|---|---|
| `bool SupportsAudio()` | Se la webcam espone un microfono. |
| `void SetAudioNodeID(int32 id)` | `-1` = auto, `0` = nessun audio, `>0` = un node id specifico del Media Kit. |

Statistiche (interrogabili in qualsiasi momento durante la cattura):

| Metodo | Scopo |
|---|---|
| `uint32 FramesCaptured()` | Frame consegnati dall'avvio. |
| `uint32 FramesDropped()` | Frame persi perché il consumatore è rimasto indietro. |
| `float CurrentFPS()` | Frame rate corrente (media mobile). |

Identificazione (USB / driver), utile per UI e diagnostica:
`Name()`, `VendorID()`, `ProductID()`, `ProductName()`, `SerialNumber()`,
`DriverName()`, `DriverVersion()`, e `GetUSBInfo()` / `GetDriverInfo()` per le
struct complete.

### Contratto del messaggio frame

`StartCapture` posta, per ogni frame, al tuo looper:

- `what`: `'frcv'` (`MSG_FRAME_RECEIVED`)
- campo `"bitmap"`: un `BBitmap*` (`FindPointer`), `B_RGB32`, **in prestito** —
  valido solo durante il messaggio, di proprietà della libreria.

Messaggi di livello audio (quando l'audio è attivo):

- `what`: `'audl'` (`MSG_WEBCAM_AUDIO_LEVEL`)
- campi `"left"` / `"right"`: livelli di picco `float` in `0.0 .. 1.0`.

> I default sono `MSG_WEBCAM_FRAME` (`'frcv'`) e `MSG_WEBCAM_AUDIO_LEVEL`
> (`'audl'`), definiti in `WebcamDevice.h` e di proprietà della libreria.
> Sovrascrivili per-cattura con i parametri di `StartCapture` qui sopra — senza
> toccare i sorgenti della libreria.

---

## Threading e ownership — le regole che fanno male

- **I bitmap sono in prestito.** Validi solo dentro il messaggio `'frcv'`. Copia
  per conservarli. Mai `delete`.
- **I device sono di proprietà del roster.** Non fare delete di `WebcamDevice*`;
  tieni vivo il `WebcamRoster` finché usi i suoi device.
- **`StopCapture()` è sincrono e thread-safe.** Dopo il ritorno, nessun altro
  messaggio frame verrà postato per quel device.
- **Un looper, un thread di handler.** Il tuo handler dei frame non deve
  bloccare. Sposta encoding/IO su un thread tuo; la libreria continua a catturare
  e semplicemente scarta i frame che non riesci a smaltire.

---

## Compilazione

Le classi sono C++ semplice con il Media Kit di Haiku. Per usarle oggi, aggiungi
i sorgenti al tuo build e linka le librerie di sistema:

```
SRCS  += WebcamRoster.cpp WebcamDevice.cpp VideoConsumer.cpp \
         AudioConsumer.cpp USBVideoParser.cpp
LIBS  += be media tracker translation device shared jpeg
```

(Quando sarà impacchettata come libreria shared/static, linka `libbubicapture` e
aggiungi i suoi header all'include path. Il contratto qui sopra non cambia.)

---

## Licenza

MIT, come BubiCam. Vedi `LICENSE`.
