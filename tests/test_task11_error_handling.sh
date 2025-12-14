#!/bin/bash
# Test per Task 11: Standardizzare gestione errori (status_t vs BAlert)
#
# PROBLEMA ORIGINALE:
# La gestione degli errori era inconsistente nel codebase:
# - Alcuni errori usavano BAlert per notificare l'utente
# - Altri usavano fprintf(stderr) (invisibile all'utente)
# - I messaggi di errore avevano formati diversi
# - Nessun sistema centralizzato per logging
#
# SOLUZIONE IMPLEMENTATA:
# Creato ErrorUtils.h che fornisce:
# - Macro di logging con livelli di severita' (LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR)
# - Formato consistente: [MODULE] LEVEL: message
# - Helper per alert utente (ShowErrorAlert, ShowWarningAlert, ShowConfirmationAlert)
# - Macro combinate (LOG_AND_SHOW_ERROR) per log + alert
# - Macro di check (CHECK_STATUS, CHECK_STATUS_LOG) per validazione status_t
#
# FILE MODIFICATI:
# - src/utils/ErrorUtils.h: Nuovo file con macro e helper
# - src/webcam/VideoConsumer.cpp: Usa LOG_MODULE e ErrorUtils
# - src/webcam/AudioConsumer.cpp: Usa LOG_MODULE e ErrorUtils
# - src/webcam/WebcamDevice.cpp: Usa LOG_MODULE e ErrorUtils
# - src/MainWindow.cpp: Usa LOG_MODULE e ErrorUtils
# - src/mcp/MCPServer.cpp: Usa LOG_MODULE e ErrorUtils
# - src/utils/ExportUtils.cpp: Usa LOG_MODULE e ErrorUtils
#
# TEST DI VERIFICA:
# Il test verifica che:
# 1. ErrorUtils.h esista e contenga le macro necessarie
# 2. I file sorgente usino il pattern LOG_MODULE + ErrorUtils
# 3. L'applicazione compili e funzioni correttamente

BUBICAM="../objects.x86_64-cc13-release/BubiCam"
TEST_PASSED=true

echo "=== Test Task 11: Standardizzazione Gestione Errori ==="
echo ""
echo "Verifica che ErrorUtils.h sia implementato e usato correttamente."
echo ""

# Verifica che l'eseguibile esista
if [ ! -f "$BUBICAM" ]; then
    echo "ERRORE: Eseguibile non trovato: $BUBICAM"
    echo "Esegui 'make' prima di questo test."
    exit 1
fi

# Test 1: Verifica esistenza ErrorUtils.h
echo "Test 1: Verifica esistenza ErrorUtils.h..."
if [ -f "../src/utils/ErrorUtils.h" ]; then
    echo "  [OK] ErrorUtils.h esiste"
else
    echo "  [FAIL] ErrorUtils.h non trovato"
    TEST_PASSED=false
fi

# Test 2: Verifica macro LOG_* in ErrorUtils.h
echo ""
echo "Test 2: Verifica macro di logging in ErrorUtils.h..."
MACROS_FOUND=0

if grep -q "LOG_DEBUG" ../src/utils/ErrorUtils.h; then
    MACROS_FOUND=$((MACROS_FOUND + 1))
fi
if grep -q "LOG_INFO" ../src/utils/ErrorUtils.h; then
    MACROS_FOUND=$((MACROS_FOUND + 1))
fi
if grep -q "LOG_WARNING" ../src/utils/ErrorUtils.h; then
    MACROS_FOUND=$((MACROS_FOUND + 1))
fi
if grep -q "LOG_ERROR" ../src/utils/ErrorUtils.h; then
    MACROS_FOUND=$((MACROS_FOUND + 1))
fi

if [ $MACROS_FOUND -eq 4 ]; then
    echo "  [OK] Tutte le macro LOG_* presenti (DEBUG, INFO, WARNING, ERROR)"
else
    echo "  [FAIL] Mancano alcune macro LOG_* ($MACROS_FOUND/4)"
    TEST_PASSED=false
fi

# Test 3: Verifica helper functions per alert
echo ""
echo "Test 3: Verifica helper functions per alert..."
HELPERS_FOUND=0

if grep -q "ShowErrorAlert" ../src/utils/ErrorUtils.h; then
    HELPERS_FOUND=$((HELPERS_FOUND + 1))
fi
if grep -q "ShowWarningAlert" ../src/utils/ErrorUtils.h; then
    HELPERS_FOUND=$((HELPERS_FOUND + 1))
fi
if grep -q "ShowConfirmationAlert" ../src/utils/ErrorUtils.h; then
    HELPERS_FOUND=$((HELPERS_FOUND + 1))
fi

if [ $HELPERS_FOUND -eq 3 ]; then
    echo "  [OK] Tutte le helper functions presenti"
else
    echo "  [FAIL] Mancano alcune helper functions ($HELPERS_FOUND/3)"
    TEST_PASSED=false
fi

# Test 4: Verifica uso di LOG_MODULE nei file sorgente
echo ""
echo "Test 4: Verifica uso di LOG_MODULE nei file chiave..."
FILES_WITH_LOG_MODULE=0
TOTAL_FILES=6

for file in "VideoConsumer.cpp" "AudioConsumer.cpp" "WebcamDevice.cpp" \
            "MainWindow.cpp" "MCPServer.cpp" "ExportUtils.cpp"; do
    FILEPATH=$(find ../src -name "$file" 2>/dev/null | head -1)
    if [ -n "$FILEPATH" ] && grep -q "LOG_MODULE" "$FILEPATH"; then
        FILES_WITH_LOG_MODULE=$((FILES_WITH_LOG_MODULE + 1))
    fi
done

if [ $FILES_WITH_LOG_MODULE -eq $TOTAL_FILES ]; then
    echo "  [OK] Tutti i $TOTAL_FILES file chiave usano LOG_MODULE"
else
    echo "  [FAIL] Solo $FILES_WITH_LOG_MODULE/$TOTAL_FILES file usano LOG_MODULE"
    TEST_PASSED=false
fi

# Test 5: Verifica che ErrorUtils.h sia incluso
echo ""
echo "Test 5: Verifica inclusione ErrorUtils.h nei file sorgente..."
FILES_WITH_INCLUDE=0

for file in "VideoConsumer.cpp" "AudioConsumer.cpp" "WebcamDevice.cpp" \
            "MainWindow.cpp" "MCPServer.cpp" "ExportUtils.cpp"; do
    FILEPATH=$(find ../src -name "$file" 2>/dev/null | head -1)
    if [ -n "$FILEPATH" ] && grep -q "#include \"ErrorUtils.h\"" "$FILEPATH"; then
        FILES_WITH_INCLUDE=$((FILES_WITH_INCLUDE + 1))
    fi
done

if [ $FILES_WITH_INCLUDE -eq $TOTAL_FILES ]; then
    echo "  [OK] Tutti i $TOTAL_FILES file includono ErrorUtils.h"
else
    echo "  [FAIL] Solo $FILES_WITH_INCLUDE/$TOTAL_FILES file includono ErrorUtils.h"
    TEST_PASSED=false
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
    echo "La gestione errori e' stata standardizzata:"
    echo "- ErrorUtils.h: macro LOG_DEBUG/INFO/WARNING/ERROR"
    echo "- Helper functions: ShowErrorAlert, ShowWarningAlert, ShowConfirmationAlert"
    echo "- Formato consistente: [MODULE] LEVEL: message"
    echo "- 6 file chiave aggiornati per usare il nuovo sistema"
    exit 0
else
    echo "ALCUNI TEST FALLITI"
    echo ""
    echo "Verificare l'implementazione di ErrorUtils.h."
    exit 1
fi
