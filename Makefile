# BubiCam - Webcam Driver Tester for Haiku OS
# MIT License

NAME = BubiCam
TYPE = APP
APP_MIME_SIG = application/x-vnd.BubiCam

SRCS = \
	src/BubiCamApp.cpp \
	src/MainWindow.cpp \
	src/views/VideoPreviewView.cpp \
	src/views/DriverInfoView.cpp \
	src/views/SyslogView.cpp \
	src/views/VUMeterView.cpp \
	src/views/WebcamControlsView.cpp \
	src/webcam/WebcamRoster.cpp \
	src/webcam/WebcamDevice.cpp \
	src/webcam/VideoConsumer.cpp \
	src/webcam/AudioConsumer.cpp \
	src/webcam/USBVideoParser.cpp \
	src/mcp/MCPServer.cpp \
	src/utils/ExportUtils.cpp \
	src/utils/IconUtils.cpp

RDEFS = resources/BubiCam.rdef

LIBS = be media tracker translation localestub device shared network $(STDCPPLIBS)

LIBPATHS =

SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/shared

LOCAL_INCLUDE_PATHS = src src/views src/webcam src/mcp src/utils

OPTIMIZE := FULL

LOCALES =

DEFINES =

WARNINGS = ALL

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS =

LINKER_FLAGS =

DRIVER_PATH =

include $(BUILDHOME)/etc/makefile-engine
