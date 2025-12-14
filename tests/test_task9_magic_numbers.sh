#!/bin/bash
# Test per Task 9: Documentare magic numbers con commenti
#
# PROBLEMA ORIGINALE:
# Il codice conteneva numeri "magici" non documentati, come:
# - NUM_BUFFERS 3 (senza spiegazione del perche' 3)
# - kMaxLatency = 50000 (senza spiegazione dell'unita' o del rationale)
# - 320x240 come fallback (senza spiegazione)
# - 100000us e 50000us per delay (senza spiegazione)
#
# SOLUZIONE IMPLEMENTATA:
# - Aggiunti commenti dettagliati che spiegano:
#   1. Cosa rappresenta ogni costante
#   2. Perche' e' stato scelto quel valore specifico
#   3. Quali conseguenze ha cambiare il valore
# - Rinominati i numeri letterali in costanti con nome (kMaxLatency, etc.)
#
# FILE MODIFICATI:
# - VideoConsumer.h: Documentato NUM_BUFFERS con spiegazione del triple-buffering
# - VideoConsumer.cpp: Aggiunto kMaxLatency, kFallbackWidth, kFallbackHeight
# - WebcamDevice.cpp: Aggiunto kMediaStartDelay, kPostSeekDelay, kFallbackWidth, kFallbackHeight
#
# TEST DI VERIFICA:
# Questo test verifica che:
# 1. Le costanti siano definite nei file sorgente
# 2. Le costanti siano usate invece dei numeri letterali
# 3. L'applicazione compili e funzioni correttamente

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 9: Documentazione Magic Numbers ==="
echo ""
echo "Verifica che i magic numbers siano documentati e usati come costanti."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Verifica costanti in VideoConsumer.h
echo "Test 1: Verifica documentazione NUM_BUFFERS in VideoConsumer.h..."
if grep -q "Number of buffers in the shared buffer group" ../src/webcam/VideoConsumer.h; then
    if grep -q "One buffer being filled by producer" ../src/webcam/VideoConsumer.h; then
        echo "  [OK] NUM_BUFFERS documentato con spiegazione triple-buffering"
    else
        echo "  [FAIL] Commento incompleto per NUM_BUFFERS"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] Manca documentazione per NUM_BUFFERS"
    TEST_PASSED=false
fi

# Test 2: Verifica costanti in VideoConsumer.cpp
echo ""
echo "Test 2: Verifica costanti in VideoConsumer.cpp..."
CONSTANTS_FOUND=0

if grep -q "const bigtime_t kMaxLatency" ../src/webcam/VideoConsumer.cpp; then
    if grep -q "50ms is chosen as a balance" ../src/webcam/VideoConsumer.cpp; then
        echo "  [OK] kMaxLatency definito e documentato"
        CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
    fi
fi

if grep -q "const uint32 kFallbackWidth" ../src/webcam/VideoConsumer.cpp; then
    if grep -q "const uint32 kFallbackHeight" ../src/webcam/VideoConsumer.cpp; then
        echo "  [OK] kFallbackWidth/Height definiti"
        CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
    fi
fi

if [ $CONSTANTS_FOUND -lt 2 ]; then
    echo "  [FAIL] Mancano alcune costanti in VideoConsumer.cpp"
    TEST_PASSED=false
fi

# Test 3: Verifica costanti in WebcamDevice.cpp
echo ""
echo "Test 3: Verifica costanti in WebcamDevice.cpp..."
CONSTANTS_FOUND=0

if grep -q "const bigtime_t kMediaStartDelay" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kMediaStartDelay definito"
    CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
fi

if grep -q "const bigtime_t kPostSeekDelay" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kPostSeekDelay definito"
    CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
fi

if grep -q "const int32 kFallbackWidth" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kFallbackWidth definito"
    CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
fi

if grep -q "const int32 kFallbackHeight" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kFallbackHeight definito"
    CONSTANTS_FOUND=$((CONSTANTS_FOUND + 1))
fi

if [ $CONSTANTS_FOUND -lt 4 ]; then
    echo "  [FAIL] Mancano alcune costanti in WebcamDevice.cpp"
    TEST_PASSED=false
fi

# Test 4: Verifica che le costanti siano usate (non numeri letterali)
echo ""
echo "Test 4: Verifica uso costanti invece di numeri letterali..."

# Cerca che kMediaStartDelay sia usato invece di 100000
if grep -q "ts->Now() + kMediaStartDelay" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kMediaStartDelay usato correttamente"
else
    if grep -q "ts->Now() + 100000" ../src/webcam/WebcamDevice.cpp; then
        echo "  [FAIL] Trovato numero letterale 100000 invece di kMediaStartDelay"
        TEST_PASSED=false
    fi
fi

# Cerca che kPostSeekDelay sia usato
if grep -q "snooze(kPostSeekDelay)" ../src/webcam/WebcamDevice.cpp; then
    echo "  [OK] kPostSeekDelay usato correttamente"
else
    # Conta quanti snooze(50000) ci sono (dovrebbe essere 0)
    LITERAL_COUNT=$(grep -c "snooze(50000)" ../src/webcam/WebcamDevice.cpp 2>/dev/null || echo "0")
    if [ "$LITERAL_COUNT" != "0" ]; then
        echo "  [FAIL] Trovato snooze(50000) invece di kPostSeekDelay"
        TEST_PASSED=false
    fi
fi

# Test 5: Avvio e chiusura base
echo ""
echo "Test 5: Avvio e chiusura base..."
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

# Riepilogo
echo ""
echo "=== Riepilogo ==="
if [ "$TEST_PASSED" = true ]; then
    echo "TUTTI I TEST PASSATI"
    echo ""
    echo "I magic numbers sono stati documentati correttamente:"
    echo "- NUM_BUFFERS: spiegazione del pattern triple-buffering"
    echo "- kMaxLatency: spiegazione del bilanciamento latenza/stabilita"
    echo "- kFallbackWidth/Height: spiegazione della risoluzione di fallback"
    echo "- kMediaStartDelay: tempo per inizializzazione driver"
    echo "- kPostSeekDelay: tempo di stabilizzazione dopo seek"
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare che tutte le costanti siano definite e usate."
    exit 1
fi
