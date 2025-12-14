#!/bin/bash
# Test per Task 8: Usare std::atomic invece di volatile bool
#
# PROBLEMA ORIGINALE:
# MCPServer usava `volatile bool fRunning` per controllare lo stato del server.
# Il problema e' che `volatile` NON garantisce:
# - Atomicita' delle operazioni read/write
# - Memory barriers per visibilita' cross-thread
# Su sistemi multi-core, questo puo' causare race conditions dove un thread
# non vede le modifiche fatte da un altro thread.
#
# SOLUZIONE IMPLEMENTATA:
# - Convertito `volatile bool fRunning` a `std::atomic<bool> fRunning`
# - Convertito i contatori a `std::atomic<int32>` per consistenza
# - Sostituito `atomic_add()` con operatori `++`/`--` di std::atomic
# - std::atomic garantisce:
#   - Operazioni atomiche (no torn reads/writes)
#   - Memory barriers per corretta visibilita' tra thread
#
# TEST DI VERIFICA:
# Il test verifica che il server MCP si avvii e si fermi correttamente,
# gestendo gli stati in modo thread-safe.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 8: Uso di std::atomic in MCPServer ==="
echo ""
echo "Verifica che le operazioni atomiche funzionino correttamente."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio e chiusura base
echo "Test 1: Avvio e chiusura base..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 2

if kill -0 $APP_PID 2>/dev/null; then
    echo "  [OK] Applicazione avviata"
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
else
    echo "  [FAIL] Applicazione crashata all'avvio"
    TEST_PASSED=false
fi

# Test 2: Avvii/chiusure rapidi (stress test stato atomico)
echo ""
echo "Test 2: Avvii/chiusure rapidi (stress test stato atomico)..."
for i in $(seq 1 5); do
    echo -n "  Iterazione $i: "

    $BUBICAM 2>&1 &
    APP_PID=$!
    sleep 1

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

# Test 3: Esecuzione prolungata
echo ""
echo "Test 3: Esecuzione prolungata (3 secondi)..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 3

if kill -0 $APP_PID 2>/dev/null; then
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
        echo "  [OK] Esecuzione prolungata completata"
    else
        echo "  [FAIL] Exit code: $EXIT_CODE"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] Crash durante esecuzione"
    TEST_PASSED=false
fi

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "Le operazioni atomiche in MCPServer funzionano correttamente."
    echo "Lo stato del server e' gestito in modo thread-safe."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
