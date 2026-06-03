# BubiCam Test Suite
# Build: make -f tests/Makefile
# Run:   objects.x86_64-cc13-release/BubiCamTests

NAME = BubiCamTests
TYPE = APP
APP_MIME_SIG = application/x-vnd.BubiCam-Tests

SRCS = tests/TestRunner.cpp

LIBS = be media translation $(STDCPPLIBS)

SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/shared

LOCAL_INCLUDE_PATHS = src src/webcam src/utils

OPTIMIZE := FULL

WARNINGS = ALL

COMPILER_FLAGS = -msse2

include $(BUILDHOME)/etc/makefile-engine
