/*
 * WebcamKit - Webcam Library for Haiku OS
 * Copyright (c) 2024 BubiCam Contributors
 * MIT License
 *
 * Main include header for applications using WebcamKit.
 *
 * Quick start:
 *   #include <WebcamKit.h>
 *
 *   WebcamRoster roster;
 *   roster.EnumerateDevices();
 *
 *   WebcamDevice* cam = roster.DeviceAt(0);
 *   cam->StartCapture(myLooper);
 *   // Handle MSG_WEBCAM_FRAME messages in your looper
 *   cam->StopCapture();
 */

#ifndef WEBCAM_KIT_H
#define WEBCAM_KIT_H

#include "WebcamDevice.h"
#include "WebcamRoster.h"
#include "VideoConsumer.h"
#include "AudioConsumer.h"
#include "USBVideoParser.h"

#endif // WEBCAM_KIT_H
