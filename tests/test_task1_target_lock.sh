#!/bin/bash
# Test per Task 1: Verifica BLocker per fTarget in VideoConsumer/AudioConsumer
#
# PROBLEMA ORIGINALE:
# In VideoConsumer::_SendFrameToTarget() e AudioConsumer::_HandleBuffer(),
# il puntatore fTarget veniva controllato per NULL e poi usato senza lock.
# In ambiente multi-thread, fTarget poteva essere eliminato tra il check e l'uso,
# causando un crash.
#
# SOLUZIONE IMPLEMENTATA:
# - Aggiunto BLocker fTargetLock per proteggere l'accesso a fTarget
# - Aggiunto metodo SetTarget() thread-safe
# - Modificati _SendFrameToTarget() e _HandleBuffer() per usare BAutolock
#
# TEST DI VERIFICA:
# Questo test esegue l'applicazione multiple volte con start/stop rapidi
# per verificare che non ci siano crash dovuti a race condition.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_ITERATIONS=5
STARTUP_WAIT=3
TEST_PASSED=true

echo "=== Test Task 1: BLocker per fTarget ==="
echo ""
echo "Verifica che il fix per la race condition funzioni correttamente."
echo "Il test eseguira' $TEST_ITERATIONS iterazioni di start/stop rapidi."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Verifica che l'app si avvii correttamente
echo "Test 1: Verifica avvio applicazione..."
$BUBICAM &
APP_PID=$!
sleep $STARTUP_WAIT

if kill -0 $APP_PID 2>/dev/null; then
    echo "  [OK] Applicazione avviata (PID: $APP_PID)"
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
else
    echo "  [FAIL] Applicazione non avviata o crashata"
    TEST_PASSED=false
fi

# Test 2: Start/stop rapidi multipli
echo ""
echo "Test 2: Start/stop rapidi ($TEST_ITERATIONS iterazioni)..."
for i in $(seq 1 $TEST_ITERATIONS); do
    echo -n "  Iterazione $i: "

    $BUBICAM &
    APP_PID=$!
    sleep 1

    if kill -0 $APP_PID 2>/dev/null; then
        kill $APP_PID 2>/dev/null
        wait $APP_PID 2>/dev/null
        EXIT_CODE=$?

        # Su Haiku, un crash genera exit code diverso da 0 o segnali
        if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
            echo "[OK]"
        else
            echo "[FAIL] Exit code: $EXIT_CODE"
            TEST_PASSED=false
        fi
    else
        echo "[FAIL] App crashata durante l'avvio"
        TEST_PASSED=false
    fi
done

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "Il fix per la race condition sembra funzionare correttamente."
    echo "La sincronizzazione con BLocker protegge l'accesso a fTarget."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
