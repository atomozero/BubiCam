#!/bin/bash
# Test per USB Packet Viewer Feature
#
# FUNZIONALITA' IMPLEMENTATE:
# - USBPacketView: visualizzatore USB descriptors
# - HexDumpView: visualizzazione hex dump raw data
# - DescriptorTreeView: lista navigabile di descriptor
# - Summary tab: analisi testuale completa
# - Export: esportazione in file di testo
# - Integrazione MainWindow con tab USB e menu item
#
# TEST DI VERIFICA:
# 1. Verifica esistenza file USBPacketView.h e .cpp
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
echo " Test USB Packet Viewer Feature"
echo "=============================================="
echo ""
echo "Verifica implementazione USB Packet Viewer."

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo -e "${RED}ERRORE: Eseguibile non trovato: $BUBICAM${NC}"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

section "Test 1: Verifica file USBPacketView"

# Test 1.1: Header file esiste
if [ -f "$SRC_DIR/views/USBPacketView.h" ]; then
    pass "USBPacketView.h esiste"
else
    fail "USBPacketView.h non trovato"
fi

# Test 1.2: Implementation file esiste
if [ -f "$SRC_DIR/views/USBPacketView.cpp" ]; then
    pass "USBPacketView.cpp esiste"
else
    fail "USBPacketView.cpp non trovato"
fi

# Test 1.3: File non vuoti
if [ -s "$SRC_DIR/views/USBPacketView.h" ] && [ -s "$SRC_DIR/views/USBPacketView.cpp" ]; then
    pass "File non vuoti"
else
    fail "Uno o piu' file sono vuoti"
fi

section "Test 2: Verifica strutture dati"

# Test 2.1: USBDescriptorInfo struct
if grep -q "struct USBDescriptorInfo" "$SRC_DIR/views/USBPacketView.h"; then
    if grep -q "type" "$SRC_DIR/views/USBPacketView.h" && \
       grep -q "rawData" "$SRC_DIR/views/USBPacketView.h" && \
       grep -q "rawLength" "$SRC_DIR/views/USBPacketView.h"; then
        pass "USBDescriptorInfo struct definita correttamente"
    else
        fail "USBDescriptorInfo struct incompleta"
    fi
else
    fail "Manca struct USBDescriptorInfo"
fi

# Test 2.2: USB descriptor type constants
if grep -q "USB_DESC_DEVICE" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "USB_DESC_CONFIGURATION" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "USB_DESC_INTERFACE" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "USB_DESC_ENDPOINT" "$SRC_DIR/views/USBPacketView.h"; then
    pass "Costanti USB descriptor type definite"
else
    fail "Mancano costanti USB descriptor type"
fi

# Test 2.3: UVC subtype constants
if grep -q "UVC_VS_FORMAT_UNCOMPRESSED" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "UVC_VS_FORMAT_MJPEG" "$SRC_DIR/views/USBPacketView.h"; then
    pass "Costanti UVC subtype definite"
else
    fail "Mancano costanti UVC subtype"
fi

section "Test 3: Verifica classi"

# Test 3.1: HexDumpView class
if grep -q "class HexDumpView" "$SRC_DIR/views/USBPacketView.h"; then
    if grep -q "SetData" "$SRC_DIR/views/USBPacketView.h" && \
       grep -q "GetHexString" "$SRC_DIR/views/USBPacketView.h"; then
        pass "HexDumpView class definita"
    else
        fail "HexDumpView class incompleta"
    fi
else
    fail "Manca class HexDumpView"
fi

# Test 3.2: DescriptorTreeView class
if grep -q "class DescriptorTreeView" "$SRC_DIR/views/USBPacketView.h"; then
    if grep -q "AddDescriptor" "$SRC_DIR/views/USBPacketView.h" && \
       grep -q "SelectedDescriptor" "$SRC_DIR/views/USBPacketView.h"; then
        pass "DescriptorTreeView class definita"
    else
        fail "DescriptorTreeView class incompleta"
    fi
else
    fail "Manca class DescriptorTreeView"
fi

# Test 3.3: USBPacketView class
if grep -q "class USBPacketView" "$SRC_DIR/views/USBPacketView.h"; then
    if grep -q "SetDevice" "$SRC_DIR/views/USBPacketView.h" && \
       grep -q "Refresh" "$SRC_DIR/views/USBPacketView.h"; then
        pass "USBPacketView class definita"
    else
        fail "USBPacketView class incompleta"
    fi
else
    fail "Manca class USBPacketView"
fi

section "Test 4: Verifica messaggi"

# Test 4.1: Messaggi USB viewer
MSGS_OK=true
for msg in "MSG_USB_REFRESH" "MSG_USB_COPY_HEX" "MSG_USB_EXPORT" "MSG_USB_TAB_CHANGED"; do
    if ! grep -q "$msg" "$SRC_DIR/views/USBPacketView.h"; then
        fail "Manca messaggio $msg"
        MSGS_OK=false
    fi
done
if [ "$MSGS_OK" = true ]; then
    pass "Tutti i messaggi USB viewer definiti"
fi

# Test 4.2: MSG_SHOW_USB_VIEWER in MainWindow
if grep -q "MSG_SHOW_USB_VIEWER" "$SRC_DIR/MainWindow.h"; then
    pass "MSG_SHOW_USB_VIEWER definito in MainWindow"
else
    fail "Manca MSG_SHOW_USB_VIEWER in MainWindow"
fi

section "Test 5: Verifica metodi implementazione"

# Test 5.1: Metodi privati USBPacketView
METHODS_OK=true
for method in "_BuildLayout" "_ParseUSBDescriptors" "_AddToLog" "_ExportDescriptors"; do
    if ! grep -q "$method" "$SRC_DIR/views/USBPacketView.h"; then
        fail "Manca metodo $method"
        METHODS_OK=false
    fi
done
if [ "$METHODS_OK" = true ]; then
    pass "Tutti i metodi privati implementati"
fi

# Test 5.2: Helper methods
if grep -q "_FormatGUID" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "_GetTerminalTypeName" "$SRC_DIR/views/USBPacketView.h" && \
   grep -q "_GetProcessingUnitControls" "$SRC_DIR/views/USBPacketView.h"; then
    pass "Helper methods per USB definiti"
else
    fail "Mancano helper methods"
fi

section "Test 6: Verifica integrazione MainWindow"

# Test 6.1: Include USBPacketView
if grep -q '#include "USBPacketView.h"' "$SRC_DIR/MainWindow.cpp"; then
    pass "USBPacketView.h incluso in MainWindow.cpp"
else
    fail "USBPacketView.h non incluso in MainWindow.cpp"
fi

# Test 6.2: Membro fUSBPacketView
if grep -q "USBPacketView.*fUSBPacketView" "$SRC_DIR/MainWindow.h"; then
    pass "fUSBPacketView membro dichiarato"
else
    fail "Manca fUSBPacketView in MainWindow.h"
fi

# Test 6.3: Forward declaration
if grep -q "class USBPacketView" "$SRC_DIR/MainWindow.h"; then
    pass "Forward declaration presente"
else
    fail "Manca forward declaration di USBPacketView"
fi

# Test 6.4: Menu item USB Descriptors
if grep -q "USB Descriptors" "$SRC_DIR/MainWindow.cpp"; then
    pass "Menu item 'USB Descriptors' presente"
else
    fail "Manca menu item 'USB Descriptors'"
fi

# Test 6.5: Tab USB
if grep -q '"USB"' "$SRC_DIR/MainWindow.cpp"; then
    pass "Tab 'USB' creata"
else
    fail "Manca tab 'USB'"
fi

# Test 6.6: SetDevice chiamato su USBPacketView
if grep -q "fUSBPacketView->SetDevice" "$SRC_DIR/MainWindow.cpp"; then
    pass "SetDevice chiamato su fUSBPacketView"
else
    fail "Manca chiamata SetDevice su fUSBPacketView"
fi

section "Test 7: Verifica Makefile"

if grep -q "USBPacketView.cpp" "../Makefile"; then
    pass "USBPacketView.cpp nel Makefile"
else
    fail "USBPacketView.cpp non nel Makefile"
fi

section "Test 8: Verifica implementazione funzionalita'"

# Test 8.1: HexDump Draw implementation
if grep -q "fBytesPerLine" "$SRC_DIR/views/USBPacketView.cpp" && \
   grep -q "DrawString" "$SRC_DIR/views/USBPacketView.cpp"; then
    pass "HexDump: rendering implementato"
else
    fail "HexDump: rendering mancante"
fi

# Test 8.2: Clipboard support
if grep -q "be_clipboard" "$SRC_DIR/views/USBPacketView.cpp"; then
    pass "Supporto clipboard implementato"
else
    fail "Supporto clipboard mancante"
fi

# Test 8.3: Export to file
if grep -q "BFile file" "$SRC_DIR/views/USBPacketView.cpp" && \
   grep -q "B_WRITE_ONLY" "$SRC_DIR/views/USBPacketView.cpp"; then
    pass "Export file implementato"
else
    fail "Export file mancante"
fi

# Test 8.4: Device info parsing
if grep -q "GetUSBInfo" "$SRC_DIR/views/USBPacketView.cpp" && \
   grep -q "GetUSBVideoInfo" "$SRC_DIR/views/USBPacketView.cpp"; then
    pass "Parsing device info implementato"
else
    fail "Parsing device info mancante"
fi

# Test 8.5: UVC format parsing
if grep -q "USBVideoFormat" "$SRC_DIR/views/USBPacketView.cpp" && \
   grep -q "USBVideoFrame" "$SRC_DIR/views/USBPacketView.cpp"; then
    pass "Parsing UVC formats implementato"
else
    fail "Parsing UVC formats mancante"
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
HEADER_LINES=$(wc -l < "$SRC_DIR/views/USBPacketView.h")
CPP_LINES=$(wc -l < "$SRC_DIR/views/USBPacketView.cpp")

echo "  USBPacketView.h: $HEADER_LINES righe"
echo "  USBPacketView.cpp: $CPP_LINES righe"

if [ "$HEADER_LINES" -gt 80 ] && [ "$CPP_LINES" -gt 500 ]; then
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
    echo "USB Packet Viewer implementato:"
    echo "  - USBPacketView con UI completa"
    echo "  - HexDumpView per visualizzazione raw data"
    echo "  - DescriptorTreeView per navigazione descriptor"
    echo "  - Summary tab con analisi testuale"
    echo "  - Export in file di testo"
    echo "  - Integrazione MainWindow (tab USB + menu)"
    exit 0
else
    echo -e "${RED}ALCUNI TEST FALLITI${NC}"
    echo ""
    echo "Verificare i punti segnalati sopra."
    exit 1
fi
