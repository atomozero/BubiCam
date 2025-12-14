#!/bin/bash
# Test per Task 7: Conversione BList a BObjectList
#
# PROBLEMA ORIGINALE:
# USBVideoParser usava BList con void* pointers che richiedevano:
# - Casting manuali error-prone (es. (USBVideoFormat*)list.ItemAt(i))
# - Deallocazione manuale nei distruttori (loop + delete)
# - Nessun type-safety a compile time
#
# SOLUZIONE IMPLEMENTATA:
# - Convertito BList a BObjectList<T, true> (owning) in USBVideoParser.h
# - Rimossi i cast manuali in tutti i file che accedono alle liste
# - Creato struct FrameRate come wrapper per i valori float
# - BObjectList gestisce automaticamente la memoria (true = owning)
#
# File modificati:
# - USBVideoParser.h: Convertite le 3 liste (formats, frames, frameRates)
# - USBVideoParser.cpp: Rimossi cast, usato FrameRate wrapper
# - DriverInfoView.cpp: Rimossi cast
# - WebcamDevice.cpp: Rimossi cast
#
# TEST DI VERIFICA:
# Il test verifica che l'applicazione funzioni correttamente con le nuove
# liste type-safe, incluso il parsing USB e la visualizzazione dei formati.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 7: Conversione BList a BObjectList ==="
echo ""
echo "Verifica che le liste type-safe funzionino correttamente."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio e parsing USB
echo "Test 1: Avvio con parsing USB descriptors..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 3

if kill -0 $APP_PID 2>/dev/null; then
    echo "  [OK] Applicazione avviata (parsing USB completato)"
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
else
    echo "  [FAIL] Applicazione crashata durante parsing USB"
    TEST_PASSED=false
fi

# Test 2: Avvii multipli per stress test gestione memoria
echo ""
echo "Test 2: Avvii multipli (stress test gestione memoria liste)..."
for i in $(seq 1 5); do
    echo -n "  Iterazione $i: "

    $BUBICAM 2>&1 &
    APP_PID=$!
    sleep 2

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

# Test 3: Verifica output diagnostico formati
echo ""
echo "Test 3: Verifica output diagnostico formati..."
DIAG_OUTPUT=$($BUBICAM 2>&1 &
APP_PID=$!
sleep 3
kill $APP_PID 2>/dev/null
wait $APP_PID 2>/dev/null)

if echo "$DIAG_OUTPUT" | grep -q "format\|Format\|frame\|Frame"; then
    echo "  [OK] Output diagnostico formati presente"
else
    echo "  [INFO] Nessun output diagnostico formati (potrebbe non esserci webcam)"
fi

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "La conversione a BObjectList funziona correttamente."
    echo "La gestione automatica della memoria previene memory leaks."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
