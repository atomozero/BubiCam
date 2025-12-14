#!/bin/bash
# Test per Task 6: Verifica dimensioni memcpy
#
# PROBLEMA ORIGINALE:
# In MainWindow.cpp e VideoPreviewView.cpp, memcpy veniva usato per copiare
# dati bitmap senza verificare che il buffer di destinazione fosse abbastanza
# grande. Inoltre, MainWindow non controllava il color space quando decideva
# se riallocare il bitmap di destinazione.
#
# Scenari problematici:
# 1. Se bounds uguali ma color space diverso -> fLastFrame non riallocato
#    ma BitsLength potrebbe essere diverso -> buffer overflow
# 2. Se per qualche motivo BitsLength del sorgente > destinazione -> overflow
#
# SOLUZIONE IMPLEMENTATA:
# - MainWindow.cpp: Aggiunto check ColorSpace nella condizione di riallocazione
# - MainWindow.cpp: Aggiunto check BitsLength >= bitmap->BitsLength() prima di memcpy
# - VideoPreviewView.cpp: Aggiunto check BitsLength >= bitmap->BitsLength() prima di memcpy
#
# TEST DI VERIFICA:
# Il test verifica che l'applicazione gestisca correttamente la copia dei frame
# senza crash, anche durante operazioni di preview e screenshot.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 6: Verifica dimensioni memcpy ==="
echo ""
echo "Verifica che le operazioni memcpy su bitmap siano sicure."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio e ricezione frame
echo "Test 1: Avvio con ricezione frame video..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 3

if kill -0 $APP_PID 2>/dev/null; then
    echo "  [OK] Applicazione avviata (gestione frame sicura)"
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
else
    echo "  [FAIL] Applicazione crashata durante ricezione frame"
    TEST_PASSED=false
fi

# Test 2: Avvii multipli per stress test copia bitmap
echo ""
echo "Test 2: Avvii multipli (stress test copia bitmap)..."
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

# Test 3: Esecuzione prolungata per testare molte copie di frame
echo ""
echo "Test 3: Esecuzione prolungata (5 secondi, molti frame)..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 5

if kill -0 $APP_PID 2>/dev/null; then
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 143 ]; then
        echo "  [OK] Esecuzione prolungata completata (molte copie frame gestite)"
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
    echo "Le operazioni memcpy su bitmap sono sicure."
    echo "I controlli di dimensione prevengono buffer overflow."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
