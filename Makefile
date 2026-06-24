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
	src/views/DriverTestView.cpp \
	src/views/USBPacketView.cpp \
	src/views/SyslogView.cpp \
	src/views/VUMeterView.cpp \
	src/views/WebcamControlsView.cpp \
	src/views/LEDView.cpp \
	src/views/DeskbarReplicant.cpp \
	src/views/PreviewReplicant.cpp \
	src/mcp/MCPServer.cpp \
	src/utils/ExportUtils.cpp \
	src/utils/IconUtils.cpp \
	src/utils/NotificationUtils.cpp \
	src/utils/VideoFilter.cpp \
	src/services/StreamServer.cpp \
	src/services/VideoRecorder.cpp

RDEFS = resources/BubiCam.rdef

LIBS = be media tracker translation localestub device shared network jpeg webcam $(STDCPPLIBS)

LIBPATHS = lib/libwebcam/objects.x86_64-cc13-release

SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/shared

LOCAL_INCLUDE_PATHS = src src/views src/webcam src/mcp src/utils src/services lib/libwebcam/include

OPTIMIZE := FULL

LOCALES = en it de zh ja

DEFINES =

WARNINGS = ALL

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS =

LINKER_FLAGS = -Wl,-rpath,'$$ORIGIN'

DRIVER_PATH =

include $(BUILDHOME)/etc/makefile-engine
