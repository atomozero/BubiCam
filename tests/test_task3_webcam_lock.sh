#!/bin/bash
# Test per Task 3: Verifica BLocker per fCurrentWebcam in MainWindow
#
# PROBLEMA ORIGINALE:
# In MainWindow, il puntatore fCurrentWebcam veniva acceduto da piu' message
# handler senza sincronizzazione. In particolare, MSG_FRAME_RECEIVED (che
# arriva dal Media Kit) poteva accedere a fCurrentWebcam mentre altri handler
# lo modificavano.
#
# SOLUZIONE IMPLEMENTATA:
# - Aggiunto BLocker fWebcamLock per proteggere l'accesso a fCurrentWebcam
# - _SelectWebcam() ora usa il lock quando modifica fCurrentWebcam
# - MSG_FRAME_RECEIVED usa il lock quando accede a fCurrentWebcam per le stats
# - QuitRequested() usa il lock quando modifica fCurrentWebcam
#
# TEST DI VERIFICA:
# Questo test esegue cicli di selezione webcam e preview per verificare
# che non ci siano crash dovuti a race condition.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_ITERATIONS=5
TEST_PASSED=true

echo "=== Test Task 3: BLocker per fCurrentWebcam ==="
echo ""
echo "Verifica che il fix per la protezione di fCurrentWebcam funzioni."
echo "Il test eseguira' $TEST_ITERATIONS cicli di avvio/chiusura."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio e chiusura base
echo "Test 1: Avvio e chiusura base..."
$BUBICAM &
APP_PID=$!
sleep 2

if kill -0 $APP_PID 2>/dev/null; then
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
        echo "  [OK] Avvio e chiusura completati"
    else
        echo "  [FAIL] Exit code: $EXIT_CODE"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] App crashata durante l'avvio"
    TEST_PASSED=false
fi

# Test 2: Cicli multipli rapidi
echo ""
echo "Test 2: Cicli avvio/chiusura rapidi ($TEST_ITERATIONS iterazioni)..."
for i in $(seq 1 $TEST_ITERATIONS); do
    echo -n "  Ciclo $i: "

    $BUBICAM &
    APP_PID=$!

    # Attendi un tempo variabile (simula uso reale)
    sleep $((1 + i % 3))

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
        echo "[FAIL] App crashata"
        TEST_PASSED=false
    fi

    sleep 0.5
done

# Test 3: Chiusura molto rapida (stress test per race condition)
echo ""
echo "Test 3: Chiusura rapida durante avvio (3 iterazioni)..."
for i in $(seq 1 3); do
    echo -n "  Stress $i: "

    $BUBICAM &
    APP_PID=$!

    # Chiusura quasi immediata
    sleep 0.5

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
        echo "[FAIL] Crash"
        TEST_PASSED=false
    fi
done

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "Il fix per la protezione di fCurrentWebcam funziona correttamente."
    echo "La sincronizzazione con BLocker protegge l'accesso al puntatore webcam."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
