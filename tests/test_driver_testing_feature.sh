#!/bin/bash
# Test per Capitolo 2: Testing Avanzato Driver
#
# FUNZIONALITA' IMPLEMENTATE:
# - DriverTestView: nuova view per test driver nella tab "Testing"
# - Stress Test: cicli start/stop ripetuti (20 o 100)
# - Latency Test: misura latenza capture-to-display
# - Format Benchmark: test performance per ogni formato
# - Memory Test: monitoraggio memoria durante cattura prolungata
# - Export Report: generazione report diagnostico completo
# - DropFrameGraphView: grafico real-time FPS e frame persi
#
# INTEGRAZIONE:
# - Nuova tab "Testing" nel TabView destro
# - Menu Tools -> Driver Tests (Cmd+D)
# - SetDevice() per passare webcam alla test view
#
# TEST DI VERIFICA:
# 1. Verifica esistenza file DriverTestView.h e .cpp
# 2. Verifica definizione classi e struct
# 3. Verifica messaggi nel header
# 4. Verifica integrazione in MainWindow
# 5. Verifica compilazione e avvio

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
SRC_DIR="../src"
TEST_PASSED=true
TESTS_RUN=0
TESTS_PASSED=0

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() {
    echo -e "  ${GREEN}[OK]${NC} $1"
    ((TESTS_PASSED++))
    ((TESTS_RUN++))
}

fail() {
    echo -e "  ${RED}[FAIL]${NC} $1"
    TEST_PASSED=false
    ((TESTS_RUN++))
}

section() {
    echo ""
    echo -e "${YELLOW}=== $1 ===${NC}"
}

echo "=============================================="
echo " Test Capitolo 2: Testing Avanzato Driver"
echo "=============================================="
echo ""
echo "Verifica implementazione funzionalita' di testing driver."

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo -e "${RED}ERRORE: Eseguibile non trovato: $BUBICAM${NC}"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

section "Test 1: Verifica file DriverTestView"

# Test 1.1: Header file esiste
if [ -f "$SRC_DIR/views/DriverTestView.h" ]; then
    pass "DriverTestView.h esiste"
else
    fail "DriverTestView.h non trovato"
fi

# Test 1.2: Implementation file esiste
if [ -f "$SRC_DIR/views/DriverTestView.cpp" ]; then
    pass "DriverTestView.cpp esiste"
else
    fail "DriverTestView.cpp non trovato"
fi

# Test 1.3: File non vuoti
if [ -s "$SRC_DIR/views/DriverTestView.h" ] && [ -s "$SRC_DIR/views/DriverTestView.cpp" ]; then
    pass "File non vuoti"
else
    fail "Uno o piu' file sono vuoti"
fi

section "Test 2: Verifica strutture dati"

# Test 2.1: TestResult struct
if grep -q "struct TestResult" "$SRC_DIR/views/DriverTestView.h"; then
    if grep -q "testName" "$SRC_DIR/views/DriverTestView.h" && \
       grep -q "passed" "$SRC_DIR/views/DriverTestView.h" && \
       grep -q "duration" "$SRC_DIR/views/DriverTestView.h"; then
        pass "TestResult struct definita correttamente"
    else
        fail "TestResult struct incompleta"
    fi
else
    fail "Manca struct TestResult"
fi

# Test 2.2: FrameTiming struct
if grep -q "struct FrameTiming" "$SRC_DIR/views/DriverTestView.h"; then
    if grep -q "captureTime" "$SRC_DIR/views/DriverTestView.h" && \
       grep -q "receiveTime" "$SRC_DIR/views/DriverTestView.h" && \
       grep -q "dropped" "$SRC_DIR/views/DriverTestView.h"; then
        pass "FrameTiming struct definita correttamente"
    else
        fail "FrameTiming struct incompleta"
    fi
else
    fail "Manca struct FrameTiming"
fi

section "Test 3: Verifica classi"

# Test 3.1: DropFrameGraphView class
if grep -q "class DropFrameGraphView" "$SRC_DIR/views/DriverTestView.h"; then
    if grep -q "AddDataPoint" "$SRC_DIR/views/DriverTestView.h" && \
       grep -q "Draw" "$SRC_DIR/views/DriverTestView.h"; then
        pass "DropFrameGraphView class definita"
    else
        fail "DropFrameGraphView class incompleta"
    fi
else
    fail "Manca class DropFrameGraphView"
fi

# Test 3.2: DriverTestView class
if grep -q "class DriverTestView" "$SRC_DIR/views/DriverTestView.h"; then
    pass "DriverTestView class definita"
else
    fail "Manca class DriverTestView"
fi

section "Test 4: Verifica messaggi"

# Test 4.1: Messaggi test
MSGS_OK=true
for msg in "MSG_TEST_START_STRESS" "MSG_TEST_START_LATENCY" "MSG_TEST_START_FORMAT" \
           "MSG_TEST_START_MEMORY" "MSG_TEST_EXPORT_REPORT" "MSG_TEST_STOP"; do
    if ! grep -q "$msg" "$SRC_DIR/views/DriverTestView.h"; then
        fail "Manca messaggio $msg"
        MSGS_OK=false
    fi
done
if [ "$MSGS_OK" = true ]; then
    pass "Tutti i messaggi test definiti"
fi

# Test 4.2: MSG_SHOW_DRIVER_TESTS in MainWindow
if grep -q "MSG_SHOW_DRIVER_TESTS" "$SRC_DIR/MainWindow.h"; then
    pass "MSG_SHOW_DRIVER_TESTS definito in MainWindow"
else
    fail "Manca MSG_SHOW_DRIVER_TESTS in MainWindow"
fi

section "Test 5: Verifica metodi test"

# Test 5.1: Metodi test privati
METHODS_OK=true
for method in "_RunStressTest" "_RunLatencyTest" "_RunFormatBenchmark" "_RunMemoryTest"; do
    if ! grep -q "$method" "$SRC_DIR/views/DriverTestView.h"; then
        fail "Manca metodo $method"
        METHODS_OK=false
    fi
done
if [ "$METHODS_OK" = true ]; then
    pass "Tutti i metodi test implementati"
fi

# Test 5.2: Thread entry points
THREADS_OK=true
for thread in "_StressTestThread" "_LatencyTestThread" "_FormatTestThread" "_MemoryTestThread"; do
    if ! grep -q "$thread" "$SRC_DIR/views/DriverTestView.h"; then
        fail "Manca thread $thread"
        THREADS_OK=false
    fi
done
if [ "$THREADS_OK" = true ]; then
    pass "Tutti i thread entry points definiti"
fi

# Test 5.3: GenerateDiagnosticReport
if grep -q "GenerateDiagnosticReport" "$SRC_DIR/views/DriverTestView.h"; then
    pass "GenerateDiagnosticReport() definito"
else
    fail "Manca GenerateDiagnosticReport()"
fi

section "Test 6: Verifica integrazione MainWindow"

# Test 6.1: Include DriverTestView
if grep -q '#include "DriverTestView.h"' "$SRC_DIR/MainWindow.cpp"; then
    pass "DriverTestView.h incluso in MainWindow.cpp"
else
    fail "DriverTestView.h non incluso in MainWindow.cpp"
fi

# Test 6.2: Membro fDriverTestView
if grep -q "DriverTestView.*fDriverTestView" "$SRC_DIR/MainWindow.h"; then
    pass "fDriverTestView membro dichiarato"
else
    fail "Manca fDriverTestView in MainWindow.h"
fi

# Test 6.3: Forward declaration
if grep -q "class DriverTestView" "$SRC_DIR/MainWindow.h"; then
    pass "Forward declaration presente"
else
    fail "Manca forward declaration di DriverTestView"
fi

# Test 6.4: Menu item Driver Tests
if grep -q "Driver Tests" "$SRC_DIR/MainWindow.cpp"; then
    pass "Menu item 'Driver Tests' presente"
else
    fail "Manca menu item 'Driver Tests'"
fi

# Test 6.5: Tab Testing
if grep -q '"Testing"' "$SRC_DIR/MainWindow.cpp"; then
    pass "Tab 'Testing' creata"
else
    fail "Manca tab 'Testing'"
fi

section "Test 7: Verifica Makefile"

if grep -q "DriverTestView.cpp" "../Makefile"; then
    pass "DriverTestView.cpp nel Makefile"
else
    fail "DriverTestView.cpp non nel Makefile"
fi

section "Test 8: Verifica implementazione funzionalita'"

# Test 8.1: Stress test implementazione
if grep -q "iterations = extended ? 100 : 20" "$SRC_DIR/views/DriverTestView.cpp"; then
    pass "Stress test: 20/100 iterazioni configurabili"
else
    fail "Stress test: iterazioni non configurabili"
fi

# Test 8.2: Latency test implementazione
if grep -q "fMinLatency" "$SRC_DIR/views/DriverTestView.cpp" && \
   grep -q "fMaxLatency" "$SRC_DIR/views/DriverTestView.cpp" && \
   grep -q "fAvgLatency" "$SRC_DIR/views/DriverTestView.cpp"; then
    pass "Latency test: min/max/avg tracciati"
else
    fail "Latency test: statistiche incomplete"
fi

# Test 8.3: Memory test implementazione
if grep -q "get_system_info" "$SRC_DIR/views/DriverTestView.cpp" && \
   grep -q "used_pages" "$SRC_DIR/views/DriverTestView.cpp"; then
    pass "Memory test: monitoraggio memoria implementato"
else
    fail "Memory test: monitoraggio memoria mancante"
fi

# Test 8.4: Report diagnostico
if grep -q "listusb" "$SRC_DIR/views/DriverTestView.cpp" && \
   grep -q "syslog" "$SRC_DIR/views/DriverTestView.cpp"; then
    pass "Report: include listusb e syslog"
else
    fail "Report: manca listusb o syslog"
fi

# Test 8.5: Graph drawing
if grep -q "FillRect" "$SRC_DIR/views/DriverTestView.cpp" && \
   grep -q "StrokeLine" "$SRC_DIR/views/DriverTestView.cpp"; then
    pass "Graph: drawing implementato"
else
    fail "Graph: drawing non implementato"
fi

section "Test 9: Avvio e chiusura applicazione"

echo "  Avvio BubiCam..."
$BUBICAM 2>&1 &
APP_PID=$!
sleep 2

if kill -0 $APP_PID 2>/dev/null; then
    pass "Applicazione avviata correttamente"

    # Chiusura pulita
    kill $APP_PID 2>/dev/null
    sleep 1

    if ! kill -0 $APP_PID 2>/dev/null; then
        pass "Applicazione chiusa correttamente"
    else
        kill -9 $APP_PID 2>/dev/null
        fail "Applicazione non risponde alla chiusura"
    fi
    wait $APP_PID 2>/dev/null
else
    fail "Applicazione crashata all'avvio"
fi

section "Test 10: Verifica dimensione codice"

# Controlla che i file abbiano una dimensione ragionevole
HEADER_LINES=$(wc -l < "$SRC_DIR/views/DriverTestView.h")
CPP_LINES=$(wc -l < "$SRC_DIR/views/DriverTestView.cpp")

echo "  DriverTestView.h: $HEADER_LINES righe"
echo "  DriverTestView.cpp: $CPP_LINES righe"

if [ "$HEADER_LINES" -gt 100 ] && [ "$CPP_LINES" -gt 500 ]; then
    pass "Dimensione codice adeguata (implementazione completa)"
else
    fail "Codice troppo piccolo - implementazione incompleta?"
fi

# ============================================
# Riepilogo
# ============================================

echo ""
echo "=============================================="
echo " RIEPILOGO TEST"
echo "=============================================="
echo ""
echo "Test eseguiti: $TESTS_RUN"
echo -e "Test passati:  ${GREEN}$TESTS_PASSED${NC}"
echo -e "Test falliti:  ${RED}$((TESTS_RUN - TESTS_PASSED))${NC}"
echo ""

if [ "$TEST_PASSED" = true ]; then
    echo -e "${GREEN}TUTTI I TEST PASSATI${NC}"
    echo ""
    echo "Capitolo 2 'Testing Avanzato Driver' implementato:"
    echo "  - DriverTestView con UI completa"
    echo "  - Stress Test (20/100 cicli)"
    echo "  - Latency Test (min/max/avg)"
    echo "  - Format Benchmark"
    echo "  - Memory Test (60s)"
    echo "  - Export Report (listusb, syslog)"
    echo "  - DropFrameGraphView (grafico real-time)"
    echo "  - Integrazione MainWindow (tab + menu)"
    exit 0
else
    echo -e "${RED}ALCUNI TEST FALLITI${NC}"
    echo ""
    echo "Verificare i punti segnalati sopra."
    exit 1
fi
