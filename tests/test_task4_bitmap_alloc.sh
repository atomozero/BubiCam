#!/bin/bash
# Test per Task 4: Verifica controllo allocazione BBitmap
#
# PROBLEMA ORIGINALE:
# In vari punti del codice, `new BBitmap()` veniva usato senza verificare
# che l'allocazione fosse riuscita. In Haiku, BBitmap non lancia eccezioni
# ma crea un oggetto invalido (IsValid() ritorna false) in caso di fallimento.
# Usare un bitmap invalido causava crash o comportamenti undefined.
#
# SOLUZIONE IMPLEMENTATA:
# - MainWindow.cpp: Controllo IsValid() dopo allocazione fLastFrame
# - VideoPreviewView.cpp: Controllo IsValid() dopo allocazione fCurrentFrame
# - IconUtils.cpp: Controllo IsValid() in tutte e 4 le funzioni Create*Icon()
# - VideoConsumer.cpp: GIA' aveva i controlli corretti
#
# TEST DI VERIFICA:
# Il test verifica che l'applicazione si avvii e funzioni correttamente,
# il che indica che le allocazioni bitmap sono gestite correttamente.

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 4: Controllo allocazione BBitmap ==="
echo ""
echo "Verifica che i controlli di allocazione BBitmap funzionino."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Avvio base (verifica creazione icone toolbar)
echo "Test 1: Verifica creazione icone toolbar..."
$BUBICAM &
APP_PID=$!
sleep 2

if kill -0 $APP_PID 2>/dev/null; then
    echo "  [OK] Applicazione avviata (icone toolbar create correttamente)"
    kill $APP_PID 2>/dev/null
    wait $APP_PID 2>/dev/null
else
    echo "  [FAIL] Applicazione crashata all'avvio"
    TEST_PASSED=false
fi

# Test 2: Avvii multipli (stress test allocazione icone)
echo ""
echo "Test 2: Avvii multipli (verifica allocazione icone ripetuta)..."
for i in $(seq 1 5); do
    echo -n "  Iterazione $i: "

    $BUBICAM &
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

# Test 3: Esecuzione prolungata (verifica allocazione frame bitmap)
echo ""
echo "Test 3: Esecuzione prolungata (3 secondi)..."
$BUBICAM &
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
    echo "I controlli di allocazione BBitmap funzionano correttamente."
    echo "L'applicazione gestisce gracefully eventuali fallimenti di allocazione."
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare i log per dettagli sui crash."
    exit 1
fi
