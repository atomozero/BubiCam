# BubiCam vs Cortex vs CodyCam -- Confronto Architetturale

Confronto tra i tre principali consumer Media Kit di Haiku per webcam: **Cortex**,
**CodyCam** e **BubiCam**. Documento utile per capire le scelte progettuali e
l'evoluzione di BubiCam.

> **Nota storica:** una versione precedente di questo documento descriveva un bug
> in BubiCam (mancanza di `BBufferGroup` in `Connected()`), causa di mancata
> ricezione frame. Il problema è stato risolto: ora BubiCam crea correttamente
> il buffer group e chiama `SetOutputBuffersFor()`, in linea con CodyCam.

---

## Acquisizione del Producer Node

| App | Metodo | Note |
|-----|--------|------|
| **Cortex** | NodeManager dinamico | Supporta nodi già attivi o dormienti |
| **CodyCam** | `GetVideoInput()` | Nodo "preferito" scelto dal sistema |
| **BubiCam** | `InstantiateDormantNode()` | Istanziazione esplicita del nodo dormiente |

`InstantiateDormantNode()` è più verboso ma dà a BubiCam pieno controllo sul nodo
istanziato, necessario per il testing driver-specific.

---

## Gestione Buffer

Tutte e tre le app ora seguono lo stesso pattern Media Kit:

```cpp
// In Connected():
status = CreateBuffers(withFormat);
if (status != B_OK)
    return status;  // BubiCam: era B_OK -- fix stability round 1

BBufferConsumer::SetOutputBuffersFor(producer, fDestination,
    fBuffers, ..., true);

// In CreateBuffers():
fBuffers = new BBufferGroup();
for (uint32 j = 0; j < 3; j++) {
    fBitmap[j] = new BBitmap(bounds, colorspace, false, true);
    buffer_clone_info info;
    info.area = area_for(fBitmap[j]->Bits());
    info.offset = 0;
    info.size = fBitmap[j]->BitsLength();
    fBuffers->AddBuffer(info);
}
```

**Differenze**:
- CodyCam usa **3 buffer fissi** a 320×240 `B_RGB32`.
- BubiCam usa **3 buffer dinamici** dimensionati sul formato negoziato.
- Cortex delega la creazione buffer al producer (più flessibile, meno controllo).

---

## Formato Video Iniziale

| App | Formato Default | Strategia |
|-----|-----------------|-----------|
| **Cortex** | Negoziazione automatica | Accetta qualsiasi formato proposto dal producer |
| **CodyCam** | 320×240 `B_RGB32` hardcoded | Fallisce se il driver non lo supporta |
| **BubiCam** | Tentativi multipli + wildcard | Itera su 15+ formati prima di rinunciare |

BubiCam ha la strategia più tollerante perché deve funzionare con driver
diversi e potenzialmente buggati -- è la sua **ragione d'essere come tester**.

---

## Time Source

| App | Implementazione |
|-----|------------------|
| **Cortex** | Time source globale + fallback |
| **CodyCam** | Avvia time source se non running, include workaround |
| **BubiCam** | Time source di sistema, con null-check (post `c7af7fa`) |

```cpp
// CodyCam -- workaround per sistemi senza audio
bool running = timeSource->IsRunning();
if (!running) {
    fMediaRoster->StartTimeSource(fTimeSourceNode, real);
    fMediaRoster->SeekTimeSource(fTimeSourceNode, 0, real);
}

// BubiCam -- null-check aggiunto dopo crash report
if (timeSource == NULL) {
    LOG_WARNING("BTimeSource is NULL, skipping start");
    return B_ERROR;
}
```

---

## Calcolo Latenza

| App | Metodo |
|-----|--------|
| **Cortex** | Per gruppo di nodi |
| **CodyCam** | `GetLatencyFor()` + `GetInitialLatencyFor()` + `estimate_max_scheduling_latency()` |
| **BubiCam** | Start time fisso `Now() + 100ms` |

L'approccio semplicistico di BubiCam funziona perché il consumer è in-process
e non c'è una catena di nodi da sincronizzare. Per uno scenario di registrazione
A/V sincronizzata si potrebbe migrare al pattern di CodyCam, ma al momento non
è necessario.

---

## Robustezza Shutdown

Area dove BubiCam ha sviluppato pattern proprietari più aggressivi:

| App | Strategia | Comportamento su driver frozen |
|-----|-----------|-------------------------------|
| **Cortex** | Stop sincrono, nessun timeout | Si pianta indefinitamente |
| **CodyCam** | Stop sincrono | Si pianta indefinitamente |
| **BubiCam** | StopNode con timeout 3s + Force Stop async + emergency exit watchdog | Mai unkillable |

Tre livelli di sicurezza in cascata:
1. `StopNodeWithTimeout(roster, node, 3s)` -- thread separato con deadline
2. `_ForceStop()` -- chiama `StopCapture()` in background thread, UI resta reattiva
3. `BubiCamApp` emergency exit watchdog -- `_exit(1)` dopo 15s senza progresso

---

## Sintesi: Quando Usare Quale

| Caso d'uso | Scelta |
|------------|--------|
| Connessioni complesse multi-nodo | **Cortex** |
| Singola webcam, formato fisso, app utente | **CodyCam** |
| Test driver, debug, formati variabili, robustezza | **BubiCam** |

BubiCam non è un sostituto di CodyCam per l'uso quotidiano: il suo scopo è
**testare e debuggare driver webcam** su Haiku, con tutte le strumentazioni
necessarie (USB packet view, syslog filter, format benchmark, cycle test).

---

## Riferimenti

- [Haiku CodyCam Source](https://github.com/haiku/haiku/tree/master/src/apps/codycam)
- [Haiku Cortex Source](https://github.com/haiku/haiku/tree/master/src/apps/cortex)
- [Haiku Media Kit Documentation](https://www.haiku-os.org/docs/api/group__media.html)
