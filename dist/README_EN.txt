================================================================================

    ____        _     _  _____
   |  _ \      | |   (_)/ ____|
   | |_) |_   _| |__  _| |     __ _ _ __ ___
   |  _ <| | | | '_ \| | |    / _` | '_ ` _ \
   | |_) | |_| | |_) | | |___| (_| | | | | | |
   |____/ \__,_|_.__/|_|\_____\__,_|_| |_| |_|

                    Webcam Driver Tester for Haiku OS

================================================================================

                        ~~~  Made in Venice, Italy  ~~~

                              .     .     .
                           .  |\   /|   /|  .
                         .   |  \_/  \_/ |   .
                        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                           ~   Gondola Software   ~
                        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

================================================================================

                              USER GUIDE

================================================================================

INTRODUCTION
------------
BubiCam is a native Haiku OS application designed to test and debug USB webcam
drivers. It shows live video, records to disk, exposes the camera over HTTP and
MCP, and runs a full test suite against the driver.


GETTING STARTED
---------------
Double-click on BubiCam or run from terminal:

    ./BubiCam

For server-only operation (no GUI):

    ./BubiCam --headless


MAIN INTERFACE
--------------

  +------------------------------------------------------------------+
  | File | Webcam | Format | Control | Tools | Tests | View | Help   |
  +------------------------------------------------------------------+
  |  [Refresh] [Start] [Stop] [Screenshot] [Record] [LED]            |
  +------------------------------------------------------------------+
  |                           |                                      |
  |   VIDEO PREVIEW           |   Tabs:                              |
  |   - zoom 1x..8x           |   - Driver Info                      |
  |   - histogram overlay     |   - Controls (brightness etc.)       |
  |   - A/B compare           |   - Driver Test (stress, latency)    |
  |   - grid overlay          |   - USB Packet inspector             |
  |   - fullscreen (Enter)    |   - Syslog (regex filter)            |
  |                           |                                      |
  +------------------------------------------------------------------+
  |  VU Meter L/R                                                    |
  +------------------------------------------------------------------+
  |  Resolution: 640x480 | FPS: 30 | Frames: 1234 | Drop: 0          |
  +------------------------------------------------------------------+


KEY FEATURES
------------

VIDEO
  - Live preview, FPS/frame/drop stats
  - MJPEG decode (libjpeg-turbo)
  - YUV422/YUV420/NV12/NV21/UYVY/B_GRAY8 conversions (SSE2 optimized)
  - Zoom (1x..8x) with mouse wheel, pan with click-drag
  - RGB histogram overlay (Cmd+H)
  - A/B comparison (Cmd+B / Cmd+Shift+B)
  - Grid overlay (rule of thirds, crosshair)
  - Fullscreen mode (Enter)
  - Always-on-top floating preview

RECORDING
  - AVI Motion JPEG video with audio track
  - Time-lapse capture (configurable interval)
  - Circular buffer "save last N seconds"
  - PNG screenshots (Cmd+P)

AUDIO
  - VU meter for webcam mic
  - Audio source selection (webcam / system input / none)
  - Audio mixed into recorded AVI

DRIVER TESTING
  - Stress test (start/stop cycles)
  - Latency test (capture-to-display ms)
  - Format benchmark
  - Memory leak test
  - Cycle test (connect/disconnect robustness)
  - Export results as CSV / JSON / diagnostic report

DEVICE INFO
  - Driver name, version, USB descriptor parsing (UVC)
  - Syslog monitor with regex filter
  - USB packet inspector with hex dump
  - Webcam controls via BParameterWeb + presets

INTEGRATION
  - MCP server on port 9847 (for Claude Code)
  - Deskbar replicant with status LED
  - Desktop replicant with live preview
  - MJPEG HTTP streaming server
  - Virtual webcam (BMediaAddOn)
  - System notifications
  - hey scripting
  - Localization: EN, IT, DE, ZH, JA
  - System theme (light/dark)


KEYBOARD SHORTCUTS
------------------
  Cmd+R         Refresh devices
  Cmd+S         Start preview
  Cmd+T         Stop preview
  Cmd+P         Screenshot
  Cmd+E         Export driver info
  Cmd+H         Toggle histogram
  Cmd+G         Toggle grid overlay
  Cmd+B         Capture reference frame
  Cmd+Shift+B   A/B compare mode
  Cmd+0         Reset zoom
  Cmd+L         Clear syslog
  Cmd+Shift+M   Restart media services
  Enter         Fullscreen
  Escape        Exit fullscreen


TYPICAL USAGE
-------------
1. Launch BubiCam
2. Pick your webcam from the Webcam menu
3. Click Start (or Cmd+S)
4. Use the right-side tabs to inspect driver behavior
5. Run tests from the Tests menu when investigating driver issues


TROUBLESHOOTING
---------------

PROBLEM: "Name not found" when selecting a webcam
  -> Tools menu -> Restart Media Services

PROBLEM: No webcams found
  -> Check `listusb` output and the Syslog tab

PROBLEM: Black video / no frames
  -> Check the Syslog tab for driver errors
  -> Try Tools -> Restart Media Services

PROBLEM: Low FPS or high frame drop
  -> Pick a lower resolution from the Format menu
  -> Verify USB bandwidth (use USB 3 hub for high-res)

PROBLEM: App appears frozen
  -> Wait up to 15 seconds: the emergency exit watchdog will
     force-terminate the process. Then relaunch.


SYSTEM REQUIREMENTS
-------------------
- Haiku OS R1/beta5 or newer (x86_64)
- UVC-compatible USB webcam
- Working Media Kit
- libjpeg-turbo (for MJPEG webcams)


================================================================================

                              INFORMATION

================================================================================

Version:       2.0
License:       MIT License
Repository:    https://github.com/atomozero/BubiCam

Developed with love in Venice, Italy
Where water meets technology!

                              ,___,
                              [O.o]
                              /)__)
                              -"--"-
                           BubiCam Team

================================================================================
                    Copyright (c) 2024-2026 BubiCam Contributors
================================================================================
