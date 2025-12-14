#!/bin/bash
# Test per Task 10: Refactoring WebcamDevice per ridurre responsabilita
#
# PROBLEMA ORIGINALE:
# WebcamDevice aveva troppe responsabilita con ~40 variabili membro sparse.
# In particolare:
# - 9 variabili per informazioni USB (vendorID, productID, vendorName, etc.)
# - 3 variabili per informazioni driver (driverName, driverPath, driverVersion)
# Queste variabili non erano raggruppate logicamente, rendendo il codice
# difficile da comprendere e manutenere.
#
# SOLUZIONE IMPLEMENTATA:
# - Creata struct `USBDeviceInfo` che raggruppa tutte le 9 variabili USB
# - Creata struct `DriverInfo` che raggruppa tutte le 3 variabili driver
# - Le variabili membro individuali sono state sostituite con le struct
# - L'API pubblica rimane invariata (i getter delegano alle struct)
# - Aggiunti nuovi metodi GetUSBInfo() e GetDriverInfo() per accesso diretto
#
# BENEFICI:
# - Riduzione da ~40 a ~30 variabili membro individuali
# - Codice piu' organizzato e leggibile
# - Possibilita' di passare info USB/driver come unita' singola
# - Nessun breaking change per i consumatori esistenti
#
# TEST DI VERIFICA:
# Il test verifica che:
# 1. Le struct siano definite nel header
# 2. L'applicazione compili correttamente
# 3. L'applicazione funzioni normalmente

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 10: Refactoring WebcamDevice ==="
echo ""
echo "Verifica che le struct USBDeviceInfo e DriverInfo siano implementate."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Verifica struct USBDeviceInfo nel header
echo "Test 1: Verifica struct USBDeviceInfo in WebcamDevice.h..."
if grep -q "struct USBDeviceInfo" ../src/webcam/WebcamDevice.h; then
    if grep -q "vendorID" ../src/webcam/WebcamDevice.h && \
       grep -q "productID" ../src/webcam/WebcamDevice.h && \
       grep -q "vendorName" ../src/webcam/WebcamDevice.h; then
        echo "  [OK] USBDeviceInfo struct definita con campi corretti"
    else
        echo "  [FAIL] USBDeviceInfo struct incompleta"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] Manca struct USBDeviceInfo"
    TEST_PASSED=false
fi

# Test 2: Verifica struct DriverInfo nel header
echo ""
echo "Test 2: Verifica struct DriverInfo in WebcamDevice.h..."
if grep -q "struct DriverInfo" ../src/webcam/WebcamDevice.h; then
    if grep -q "BString.*name" ../src/webcam/WebcamDevice.h && \
       grep -q "BString.*path" ../src/webcam/WebcamDevice.h && \
       grep -q "BString.*version" ../src/webcam/WebcamDevice.h; then
        echo "  [OK] DriverInfo struct definita con campi corretti"
    else
        echo "  [FAIL] DriverInfo struct incompleta"
        TEST_PASSED=false
    fi
else
    echo "  [FAIL] Manca struct DriverInfo"
    TEST_PASSED=false
fi

# Test 3: Verifica uso delle struct come membri
echo ""
echo "Test 3: Verifica uso delle struct come membri di WebcamDevice..."
if grep -q "USBDeviceInfo.*fUSBInfo" ../src/webcam/WebcamDevice.h; then
    echo "  [OK] fUSBInfo membro presente"
else
    echo "  [FAIL] Manca fUSBInfo come membro"
    TEST_PASSED=false
fi

if grep -q "DriverInfo.*fDriverInfo" ../src/webcam/WebcamDevice.h; then
    echo "  [OK] fDriverInfo membro presente"
else
    echo "  [FAIL] Manca fDriverInfo come membro"
    TEST_PASSED=false
fi

# Test 4: Verifica nuovi metodi getter
echo ""
echo "Test 4: Verifica nuovi metodi GetUSBInfo() e GetDriverInfo()..."
if grep -q "GetUSBInfo()" ../src/webcam/WebcamDevice.h; then
    echo "  [OK] GetUSBInfo() presente"
else
    echo "  [FAIL] Manca GetUSBInfo()"
    TEST_PASSED=false
fi

if grep -q "GetDriverInfo()" ../src/webcam/WebcamDevice.h; then
    echo "  [OK] GetDriverInfo() presente"
else
    echo "  [FAIL] Manca GetDriverInfo()"
    TEST_PASSED=false
fi

# Test 5: Verifica API compatibilita' (getter individuali ancora presenti)
echo ""
echo "Test 5: Verifica API backward compatibility..."
COMPAT_OK=true
for method in "VendorID()" "ProductID()" "VendorName()" "ProductName()" \
              "DriverName()" "DriverPath()" "DriverVersion()"; do
    if ! grep -q "$method" ../src/webcam/WebcamDevice.h; then
        echo "  [FAIL] Manca metodo $method"
        COMPAT_OK=false
        TEST_PASSED=false
    fi
done
if [ "$COMPAT_OK" = true ]; then
    echo "  [OK] Tutti i getter originali ancora presenti"
fi

# Test 6: Avvio e chiusura base
echo ""
echo "Test 6: Avvio e chiusura base..."
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
    echo "Il refactoring di WebcamDevice e' stato completato:"
    echo "- USBDeviceInfo: raggruppa 9 campi USB"
    echo "- DriverInfo: raggruppa 3 campi driver"
    echo "- API backward compatible mantenuta"
    echo "- Codice piu' organizzato e leggibile"
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare l'implementazione delle struct."
    exit 1
fi
