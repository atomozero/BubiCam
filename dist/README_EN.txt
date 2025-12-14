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
BubiCam is a native Haiku OS application designed to test USB webcam drivers.
It allows you to view the video stream, control webcam parameters, and monitor
driver status.


GETTING STARTED
---------------
Double-click on BubiCam or run from terminal:

    ./BubiCam


MAIN INTERFACE
--------------

  +------------------------------------------------------------------+
  | File | Webcam | Control | Tools | Help                           |
  +------------------------------------------------------------------+
  |  [Refresh]  [Start]  [Stop]  [Screenshot]                        |
  +------------------------------------------------------------------+
  |                           |                                      |
  |                           |   [Tab: Driver Info]                 |
  |    VIDEO PREVIEW          |   - Device name                      |
  |                           |   - Supported formats                |
  |    (video stream          |   - Connection status                |
  |     appears here)         |                                      |
  |                           |   [Tab: Syslog]                      |
  |                           |   - Debug messages                   |
  |                           |                                      |
  +------------------------------------------------------------------+
  |  Resolution: 640x480  |  FPS: 30  |  Frames: 1234  |  Drop: 0    |
  +------------------------------------------------------------------+


MENUS AND FUNCTIONS
-------------------

FILE MENU:
  - Screenshot (Alt+S)     Save current frame as PNG image
  - Export Info            Export driver information as TXT
  - Export Info JSON       Export information in JSON format
  - Quit (Alt+Q)           Close the application

WEBCAM MENU:
  - List of detected webcams in the system
  - Select a webcam to activate it

CONTROL MENU:
  - Start Preview          Begin video capture
  - Stop Preview           Stop video capture
  - Refresh Devices        Rescan available webcams
  - Video Format           Select resolution and format

TOOLS MENU:
  - Restart Media Server   Useful when webcam is unresponsive
  - Show Controls          Open webcam controls panel


TYPICAL USAGE
-------------

1. Launch BubiCam

2. From the "Webcam" menu, select your webcam from the list

3. Click "Start Preview" or press the Start button in the toolbar

4. Video preview appears in the left panel

5. Use the "Driver Info" tab to see technical details

6. Use "Screenshot" to save a frame


TROUBLESHOOTING
---------------

PROBLEM: "Name not found" or webcam not detected
SOLUTION: Tools menu -> Restart Media Server

PROBLEM: Black video or no frames
SOLUTION:
  1. Verify the webcam is connected
  2. Check the Syslog tab for error messages
  3. Try restarting the Media Server

PROBLEM: Low FPS or high frame drop
SOLUTION:
  1. Select a lower resolution from the Format menu
  2. Close other applications using the webcam


SUPPORTED VIDEO FORMATS
-----------------------
- B_RGB32      (32-bit RGB)
- B_RGB24      (24-bit RGB)
- B_YCbCr422   (YUYV)
- B_YCbCr420   (I420/YUV420P)


SYSTEM REQUIREMENTS
-------------------
- Haiku OS (x86_64)
- UVC compatible USB webcam
- Working Media Kit


================================================================================

                              INFORMATION

================================================================================

Version:       1.0
License:       MIT License
Repository:    https://github.com/user/BubiCam

Developed with love in Venice, Italy
Where water meets technology!

                              ,___,
                              [O.o]
                              /)__)
                              -"--"-
                           BubiCam Team

================================================================================
                    Copyright (c) 2024 BubiCam Contributors
================================================================================
