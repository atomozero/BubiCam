# BubiCam - Webcam Driver Tester for Haiku OS

**Version 1.0**

A native Haiku OS application for testing and debugging USB webcam drivers.

![BubiCam Screenshot](img/screenshot133.png)

## Features

- **Live Video Preview**: Real-time video stream display from connected webcams with FPS and frame statistics
- **Native Toolbar**: Quick access buttons for refresh, start/stop preview, and screenshot
- **Driver Information**: Comprehensive display of driver and device details:
  - USB device information (VID, PID, class, subclass, protocol)
  - Driver name, path, and version
  - Supported video formats and resolutions
  - USB Video Class (UVC) descriptor parsing
  - Audio capabilities
  - Media Kit registration status
  - Driver diagnostics and error analysis
- **Syslog Monitor**: Real-time filtering and display of webcam/USB related syslog entries with noise filtering
- **Audio VU Meter**: Visual stereo indicator for webcam microphone levels with peak hold
- **Multiple Webcam Support**: Switch between different connected webcams
- **Webcam Controls**: Adjust brightness, contrast, saturation, and other parameters exposed by the driver
- **Screenshot**: Capture and save video frames as PNG images
- **Export**: Save driver information as text or JSON for debugging and bug reports
- **Format Selection**: Choose between available video formats and resolutions
- **Driver Bug Detection**: Automatic detection and reporting of known driver issues

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
3. Click the **Start** button or use **Control > Start Preview** to begin video capture
4. View driver information in the **Driver Info** tab
5. Adjust webcam settings in the **Controls** tab
6. Monitor syslog for USB/webcam related messages in the **Syslog** panel
7. Check the VU meter for microphone activity

### Keyboard Shortcuts

- `Cmd+R`: Refresh device list
- `Cmd+S`: Start preview
- `Cmd+T`: Stop preview
- `Cmd+P`: Take screenshot
- `Cmd+E`: Export driver info as text
- `Cmd+Shift+E`: Export driver info as JSON
- `Cmd+K`: Show controls panel
- `Cmd+L`: Clear syslog
- `Cmd+Shift+M`: Restart media services
- `Cmd+Q`: Quit application

### Tools Menu

- **Clear Syslog**: Clear the syslog view
- **Filter Syslog Noise**: Toggle filtering of noisy USB host controller messages (enabled by default)
- **Restart Media Services**: Restart media_server if webcam becomes unresponsive

## Architecture

```
BubiCam/
├── src/
│   ├── BubiCamApp.cpp/h        # Application entry point
│   ├── MainWindow.cpp/h        # Main window and layout
│   ├── views/
│   │   ├── VideoPreviewView    # Video display widget with stats
│   │   ├── DriverInfoView      # Driver information text view
│   │   ├── SyslogView          # Syslog monitoring with filtering
│   │   ├── VUMeterView         # Audio level meter
│   │   └── WebcamControlsView  # Webcam parameter controls
│   ├── webcam/
│   │   ├── WebcamRoster        # Device enumeration
│   │   ├── WebcamDevice        # Individual device management
│   │   ├── VideoConsumer       # Media Kit video buffer consumer
│   │   └── AudioConsumer       # Media Kit audio buffer consumer
│   └── utils/
│       ├── ExportUtils         # Screenshot and export utilities
│       ├── IconUtils           # Toolbar icon generation
│       └── USBVideoParser      # UVC descriptor parsing
├── resources/
│   └── BubiCam.rdef            # Application resources and icon
├── Makefile
├── LICENSE
└── README.md
```

## Technical Details

### Media Kit Integration

BubiCam uses Haiku's Media Kit to:
- Enumerate video producer nodes via BMediaRoster
- Query device capabilities and formats
- Receive video frames through BBufferConsumer
- Access webcam parameter controls via BParameterWeb
- Handle audio input for VU meter display

### USB Device Information

USB device details are gathered using:
- `BUSBRoster` for USB device enumeration
- Media node queries for format information
- Parameter web inspection for driver-exposed settings
- USB Video Class descriptor parsing for detailed format info

### Syslog Monitoring

The syslog monitor:
- Watches `/var/log/syslog` for changes
- Filters for USB, video, media, and camera-related entries
- Color-codes entries by type (errors in red, USB in green, media in blue)
- Filters out noisy USB host controller messages (configurable)
- Removes duplicate consecutive entries

### Export Formats

Driver information can be exported in two formats:
- **Text**: Human-readable report with all device details
- **JSON**: Machine-readable format for scripting and automation

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
4. Try **Tools > Restart Media Services** if the webcam becomes unresponsive

### "Name not found" error when selecting webcam

This usually means the webcam is blocked by a previous session:
1. Use **Tools > Restart Media Services** to reset the media system
2. BubiCam will offer to restart automatically when this error is detected

### Audio not working

1. Verify the webcam has a built-in microphone
2. Check Media preferences for audio input devices
3. Some webcams expose audio as separate devices

### Controls not working

1. Not all webcam drivers expose adjustable parameters
2. Check if parameters are available in Media preferences
3. Some parameters may only work during active capture

## Known Issues

Some USB webcam drivers have bugs in their cleanup code that may cause crashes after BubiCam exits. BubiCam detects these cases and:
- Warns the user before exit
- Generates a diagnostic report on the Desktop
- The crash (if it occurs) is harmless and doesn't affect your data

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Acknowledgments

- Haiku OS team for the excellent Be API documentation
- UVC driver developers for webcam support in Haiku
