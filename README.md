# BubiCam

Native webcam tester for Haiku OS: preview, record, and stress-test USB webcams to debug drivers, inspect UVC descriptors, and diagnose capture problems without leaving the desktop.

![BubiCam on Haiku](img/screenshot200.png)

If BubiCam saves you time, consider supporting development: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)


## Features

* Live video preview with FPS, frame count, and drop stats
* MJPEG decode plus SSE2-optimized YUV422/420, NV12/NV21, and UYVY conversions
* Zoom (1x-8x), pan, RGB histogram, grid overlay, and A/B format comparison
* Fullscreen mode and a floating always-on-top preview
* Video recording to AVI (Motion JPEG) with audio, time-lapse, and circular "save last N seconds" buffer
* Screenshot (PNG) and raw pre-conversion frame export for driver debugging
* Audio VU meter and selectable audio source (webcam mic, system input, or none)
* Driver test suite: stress, latency, format benchmark, memory leak, and hot-plug cycle tests
* Export test results and diagnostic reports as CSV or JSON
* Driver/USB device info, UVC descriptor parsing, and a filtered syslog monitor
* MJPEG HTTP streaming, Deskbar and Desktop replicants, and a virtual webcam for other apps
* MCP server (port 9847) for Claude Code integration and `hey` scripting support
* Headless command-line mode, system theme support, and localization (EN, IT, DE, ZH, JA)
* Reusable `libwebcam.so` shared library with a public API in `lib/libwebcam/include/`

## Quick start

```
make
./objects.x86_64-cc13-release/BubiCam
```

Then:
1. Select a webcam from the **Webcam** menu
2. Click **Start** to begin the live preview
3. Check the **Driver Info** tab for device details and **Controls** to adjust settings
4. Use the **Testing** tab to run the driver test suite

Run headless (no GUI) with:

```
./objects.x86_64-cc13-release/BubiCam --headless
```

### Shortcuts

| Key | Action | Key | Action |
|-----|--------|-----|--------|
| Cmd+R | Refresh devices | Cmd+H | Toggle histogram |
| Cmd+S | Start preview | Cmd+G | Toggle grid overlay |
| Cmd+T | Stop preview | Cmd+B | Capture reference frame |
| Cmd+P | Screenshot | Cmd+Shift+B | A/B compare mode |
| Cmd+E | Export info | Cmd+0 | Reset zoom |
| Cmd+L | Clear syslog | Enter / Esc | Enter / exit fullscreen |

## Build

Requires Haiku with GCC and standard system libraries (`libbe`, `libmedia`,
`libdevice`, `libtracker`, `libnetwork`, `libjpeg`). The bundled
`libwebcam.so` is built automatically.

```
make               # build the app and libwebcam.so
make install       # install to ~/config/apps/
make clean && make # clean rebuild
```

Or copy `objects.x86_64-cc13-release/BubiCam` to `~/config/apps/`.

## Troubleshooting

* **No webcams found** — check `listusb` output and the syslog for driver messages.
* **"Name not found" error** — use Tools → Restart Media Services.
* **No video frames** — the UVC driver may have chosen insufficient USB bandwidth; BubiCam detects this after 4 seconds and offers a lower resolution. Look for `WaitFrame TIMEOUT` in the syslog.
* **App appears frozen** — wait up to 15 seconds; the emergency-exit watchdog force-terminates the process (a `kill -9` also works afterwards).
* **Recording has no audio, or the media server crashes when enabling audio** — some Haiku audio drivers (notably HD Audio via `MultiAudioNode`) divide by `channel_count` in `Connect()` and hit a divide-by-zero that crashes `media_addon_server`. To stay safe, **Auto (Webcam Mic)** only uses the webcam's own microphone, so a webcam without a mic records video-only. Selecting the *system* audio input explicitly can still crash such a driver — if it happens, restart media services (Tools → Restart Media Services) or reboot to recover audio. This is a Haiku driver bug, not a BubiCam one.

## Documentation

* [Developer Guide](docs/DEVELOPER.md) — architecture, internals, build details
* [Roadmap](docs/ROADMAP_v2.md) — planned features and direction
* [Comparison](docs/COMPARISON.md) — BubiCam vs Cortex vs CodyCam
* [libwebcam API](docs/libwebcam/README.md) — reusable capture library

## Be careful
> **Developer's Note**: This software may contain traces of peanuts and LLM. It has been developed with passion for the Haiku platform.

## Support

If you find this project useful, you can buy me a coffee: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)

## License

MIT
