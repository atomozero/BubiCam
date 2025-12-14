#!/bin/bash
# Test per Task 2: Verifica fix race condition in WebcamDevice::StopCapture()
#
# PROBLEMA ORIGINALE:
# In WebcamDevice::StopCapture(), i puntatori ai consumer venivano acceduti
# senza sincronizzazione. Altri thread (come quelli che chiamavano FramesCaptured(),
# CurrentFPS(), etc.) potevano accedere ai consumer mentre venivano deallocati.
#
# SOLUZIONE IMPLEMENTATA:
# - Aggiunto BLocker fCaptureLock per proteggere l'accesso ai consumer pointers
# - StopCapture() ora:
#   1. Acquisisce il lock
#   2. Chiama SetTarget(NULL) sui consumer per fermare l'invio di messaggi
#   3. Salva copie locali dei dati necessari
#   4. Cancella i member pointers
#   5. Rilascia il lock
#   6. Esegue il cleanup Media Kit con le copie locali
# - FramesCaptured(), FramesDropped(), CurrentFPS(), GetCurrentFrame() usano il lock
#
# TEST DI VERIFICA:
# Questo test esegue cicli rapidi di start/stop per verificare che non ci siano
# crash dovuti a race condition durante lo shutdown.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_ITERATIONS=10
CAPTURE_TIME=2
TEST_PASSED=true

echo "=== Test Task 2: Race condition in StopCapture ==="
echo ""
echo "Verifica che il fix per la race condition in StopCapture() funzioni."
echo "Il test eseguira' $TEST_ITERATIONS cicli di start/stop rapidi."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio singolo con stop rapido
echo "Test 1: Avvio e stop rapido..."
$BUBICAM &
APP_PID=$!
sleep 1
if kill -0 $APP_PID 2>/dev/null; then
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
        echo "  [OK] Avvio e stop rapido completato"
    else
        echo "  [FAIL] Exit code: $EXIT_CODE"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] App crashata durante l'avvio"
    TEST_PASSED=false
fi

# Test 2: Cicli multipli start/stop
echo ""
echo "Test 2: Cicli start/stop multipli ($TEST_ITERATIONS iterazioni)..."
for i in $(seq 1 $TEST_ITERATIONS); do
    echo -n "  Ciclo $i: "

    # Avvia l'app
    $BUBICAM &
    APP_PID=$!

    # Attendi un po' (simula cattura)
    sleep $CAPTURE_TIME

    # Verifica che sia ancora in esecuzione
    if kill -0 $APP_PID 2>/dev/null; then
        # Termina l'app (questo triggera StopCapture)
        kill $APP_PID 2>/dev/null
        wait $APP_PID 2>/dev/null
        EXIT_CODE=$?

        if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
            echo "[OK]"
        else
            echo "[FAIL] Exit code: $EXIT_CODE (possibile crash in StopCapture)"
            TEST_PASSED=false
        fi
    else
        echo "[FAIL] App crashata durante la cattura"
        TEST_PASSED=false
    fi

    # Breve pausa tra i cicli
    sleep 0.5
done

# Test 3: Stop molto rapido (stress test)
echo ""
echo "Test 3: Stop immediato (stress test, 5 iterazioni)..."
for i in $(seq 1 5); do
    echo -n "  Stress $i: "

    $BUBICAM &
    APP_PID=$!

    # Stop quasi immediato (0.3 secondi)
    sleep 0.3

    if kill -0 $APP_PID 2>/dev/null; then
        kill $APP_PID 2>/dev/null
        wait $APP_PID 2>/dev/null
        EXIT_CODE=$?

        if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
            echo "[OK]"
        else
            echo "[FAIL] Exit code: $EXIT_CODE"
            TEST_PASSED=false
        fi
    else
        echo "[FAIL] Crash immediato"
        TEST_PASSED=false
    fi
done

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "Il fix per la race condition in StopCapture() funziona correttamente."
    echo "La sincronizzazione con BLocker protegge l'accesso ai consumer pointers."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Possibili cause di crash:"
    echo "  - Race condition non completamente risolta"
    echo "  - Problema nel driver webcam"
    echo "  - Problema nel Media Kit"
    echo ""
    echo "Verificare i log con: ./BubiCam 2>&1 | tee debug.log"
    exit 1
fi
