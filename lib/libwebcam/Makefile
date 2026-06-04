# WebcamKit - Shared library for Haiku OS webcam access
# Copyright (c) 2024 BubiCam Contributors
# MIT License

NAME = libwebcam.so
TYPE = SHARED

SRCS = \
	../../src/webcam/WebcamDevice.cpp \
	../../src/webcam/VideoConsumer.cpp \
	../../src/webcam/AudioConsumer.cpp \
	../../src/webcam/WebcamRoster.cpp \
	../../src/webcam/USBVideoParser.cpp \
	../../src/webcam/VirtualProducer.cpp

RDEFS =

LIBS = be media device shared jpeg

SYSTEM_INCLUDE_PATHS = \
	/boot/system/develop/headers/private/shared

LOCAL_INCLUDE_PATHS = \
	include \
	../../src/webcam

# The webcam sources use #include "ErrorUtils.h" - we provide a
# compatibility redirect in include/ErrorUtils.h -> WebcamLog.h
DEFINES =

OPTIMIZE := FULL

COMPILER_FLAGS = -Wall -Wno-multichar -Wno-ctor-dtor-privacy -fPIC

include $(shell findpaths -r makefile_engine B_FIND_PATH_DEVELOP_DIRECTORY)/etc/makefile-engine
