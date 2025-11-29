# BubiCam - Webcam Driver Tester for Haiku OS

A native Haiku OS application for testing and debugging USB webcam drivers.

## Features

- **Live Video Preview**: Real-time video stream display from connected webcams
- **Driver Information**: Comprehensive display of driver and device details:
  - USB device information (VID, PID, class, subclass, protocol)
  - Driver name, path, and version
  - Supported video formats and resolutions
  - Audio capabilities
  - Media Kit registration status
- **Syslog Monitor**: Real-time filtering and display of webcam/USB related syslog entries
- **Audio VU Meter**: Visual indicator for webcam microphone levels
- **Multiple Webcam Support**: Switch between different connected webcams

## Building

### Requirements

- Haiku OS (R1 Beta or later)
- GCC compiler (included with Haiku)
- Haiku development tools

### Compile

```bash
cd BubiCam
make
```

### Install

```bash
make install
```

Or manually copy the `BubiCam` binary to `/boot/system/apps/` or `~/config/apps/`.

## Usage

1. Launch BubiCam from the Applications menu or Terminal
2. Select a webcam from the **Webcam** menu
3. Use **Control > Start Preview** to begin video capture
4. View driver information in the right panel
5. Monitor syslog for USB/webcam related messages
6. Check the VU meter for microphone activity

### Keyboard Shortcuts

- `Cmd+R`: Refresh device list
- `Cmd+S`: Start preview
- `Cmd+T`: Stop preview
- `Cmd+Q`: Quit application

## Architecture

```
BubiCam/
├── src/
│   ├── BubiCamApp.cpp/h      # Application entry point
│   ├── MainWindow.cpp/h      # Main window and layout
│   ├── views/
│   │   ├── VideoPreviewView  # Video display widget
│   │   ├── DriverInfoView    # Driver information text view
│   │   ├── SyslogView        # Syslog monitoring view
│   │   └── VUMeterView       # Audio level meter
│   └── webcam/
│       ├── WebcamRoster      # Device enumeration
│       └── WebcamDevice      # Individual device management
├── resources/
│   └── BubiCam.rdef          # Application resources
├── Makefile
├── LICENSE
└── README.md
```

## Technical Details

### Media Kit Integration

BubiCam uses Haiku's Media Kit to:
- Enumerate video producer nodes
- Query device capabilities and formats
- Receive video frames from webcams

### USB Device Information

USB device details are gathered using:
- `BUSBRoster` for USB device enumeration
- Media node queries for format information
- Parameter web inspection for driver-exposed settings

### Syslog Monitoring

The syslog monitor:
- Watches `/var/log/syslog` for changes
- Filters for USB, video, media, and camera-related entries
- Color-codes entries by type (errors in red, USB in green, media in blue)

## Troubleshooting

### No webcams detected

1. Check that your webcam is connected and powered
2. Look for USB device entries in `listusb` output
3. Check syslog for driver loading messages
4. Some webcams may require additional drivers

### Video preview not working

1. Ensure the webcam driver supports the Media Kit
2. Check if the device appears in Media preferences
3. Review syslog for error messages during capture

### Audio not working

1. Verify the webcam has a built-in microphone
2. Check Media preferences for audio input devices
3. Some webcams expose audio as separate devices

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- Haiku OS team for the excellent Be API documentation
- UVC driver developers for webcam support in Haiku
