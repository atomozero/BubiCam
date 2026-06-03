# libbubicapture

A small Haiku capture library: it enumerates USB webcams through the Media Kit
and hands you decoded video frames (and audio levels) with no Media Kit
boilerplate.

It is the `src/webcam/` component of BubiCam, packaged so other applications can
reuse it. BubiCam is one consumer; your app can be another.

> Status: this document describes the public contract of the capture component.
> The code lives in `src/webcam/`. When extracted as a standalone library it
> keeps the same classes and the same contract described here.

---

## What it does for you

- Finds webcams registered with the Media Kit (`WebcamRoster`).
- Connects to a device and runs the capture pipeline (`WebcamDevice`).
- Converts every input format (MJPEG, YUYV, I420, NV12, NV21, UYVY, RGB) to a
  single, ready-to-use pixel format and posts each frame to your `BLooper`.
- Reports capture statistics (FPS, frames captured, frames dropped).
- Reports audio peak levels (left/right) for VU meters.

What it does **not** do: encoding, recording, network streaming, or UI. Those
are the application's job. The library only gives you clean frames.

---

## Dependencies

Haiku system libraries only — no third-party packages:

```
be media tracker translation device shared jpeg
```

`media` does the capture, `jpeg` (libjpeg-turbo) decodes MJPEG webcams.

---

## The mental model (read this first)

The library is **push-based** and built on Haiku's looper/messaging model. You
do not poll for frames. You give the device a `BLooper`, and the library
**posts a `BMessage` to it for every frame**, from a Media Kit thread.

```
  webcam (Media Kit producer)
        │  raw buffers
        ▼
  WebcamDevice  ──►  internal VideoConsumer  ──►  format conversion
                                                        │
                                  BMessage('frcv') + "bitmap" pointer
                                                        ▼
                                              YOUR BLooper::MessageReceived()
```

Three facts that follow from this, and that you must respect:

1. **Your handler runs on the looper thread, not a capture thread.** Standard
   Haiku rules apply — lock windows before touching views, keep the handler
   short.
2. **The frame `BBitmap` is owned by the library and is only valid inside that
   message.** If you need the pixels after returning, **copy them**. Do not
   `delete` the bitmap and do not store the pointer.
3. **Frames are dropped, not queued, when you fall behind.** The pipeline uses
   3 buffers; a slow handler means dropped frames (see `FramesDropped()`), never
   unbounded memory growth.

---

## Frame format

Every delivered frame is a `BBitmap` in **`B_RGB32`** (BGRA byte order in
memory, 4 bytes per pixel). One format, always — you never branch on the input
codec. Get dimensions from `bitmap->Bounds()` and pixels from `bitmap->Bits()`
with `bitmap->BytesPerRow()`.

---

## Quick start

A minimal headless capturer: enumerate, start the first device, count frames.

```cpp
#include <Application.h>
#include <Looper.h>
#include <Bitmap.h>
#include "WebcamRoster.h"
#include "WebcamDevice.h"

// The frame message constant the library posts (see "Frame message contract").
static const uint32 kFrameReceived = 'frcv';

class CaptureLooper : public BLooper {
public:
    CaptureLooper() : BLooper("capture") {}

    void MessageReceived(BMessage* msg) override {
        if (msg->what == kFrameReceived) {
            BBitmap* frame = NULL;
            if (msg->FindPointer("bitmap", (void**)&frame) == B_OK && frame) {
                // 'frame' is valid ONLY here. Copy if you need to keep it.
                fCount++;
            }
            return;
        }
        BLooper::MessageReceived(msg);
    }

    int32 fCount = 0;
};

int main() {
    BApplication app("application/x-vnd.example-capture");

    WebcamRoster roster;
    roster.EnumerateDevices();
    if (roster.CountDevices() == 0)
        return 1;

    WebcamDevice* dev = roster.DeviceAt(0);

    CaptureLooper* looper = new CaptureLooper();
    looper->Run();

    dev->StartCapture(looper);   // frames now flow to looper
    app.Run();                   // ... do work ...
    dev->StopCapture();
    return 0;
}
```

That is the whole integration surface: enumerate, `StartCapture(looper)`, read
frames in `MessageReceived`, `StopCapture()`.

---

## API reference

### `WebcamRoster` — find devices

`WebcamRoster` is a `BHandler`. Enumeration works standalone; live hot-plug
notifications require adding it to a running `BLooper`.

| Method | Purpose |
|---|---|
| `status_t EnumerateDevices()` | Scan the Media Kit for webcams. Call once at startup, or after a hot-plug event. |
| `int32 CountDevices()` | Number of webcams found. |
| `WebcamDevice* DeviceAt(int32 i)` | Device by index. Owned by the roster — do not delete. |
| `WebcamDevice* DeviceByName(const char* name)` | Device by name, or `NULL`. |
| `status_t StartWatching()` / `void StopWatching()` | Enable/disable hot-plug notifications. |

When devices change, the roster posts `MSG_DEVICES_CHANGED` (`'dvch'`) to the
looper it is attached to. Re-run `EnumerateDevices()` and refresh your list.

### `WebcamDevice` — capture from one webcam

Capture control:

| Method | Purpose |
|---|---|
| `status_t StartCapture(BLooper* target, uint32 frameMessage = MSG_WEBCAM_FRAME, uint32 audioLevelMessage = MSG_WEBCAM_AUDIO_LEVEL)` | Connect the pipeline and start posting frames to `target`. Pass your own `what` codes to integrate with a custom message protocol; the defaults keep `'frcv'`/`'audl'`. |
| `void StopCapture()` | Stop and disconnect. Safe to call from any thread. |
| `bool IsCapturing()` | Whether capture is running. |

To capture audio samples (not just levels) — for recording or encoding — give
the device an `AudioSink`: `device->SetAudioSink(mySink)`. The sink receives raw
PCM directly from the audio thread (see `AudioSink.h`).

Format selection (call **before** `StartCapture`):

| Method | Purpose |
|---|---|
| `const BObjectList<VideoFormat>& SupportedFormats()` | Resolutions/frame rates the device advertises. |
| `void SetRequestedFormat(const VideoFormat&)` | Ask for a specific resolution/rate. The driver may negotiate something close. |
| `VideoFormat CurrentFormat()` | The negotiated format. Verify after the first frames arrive. |

Audio source (call **before** `StartCapture`):

| Method | Purpose |
|---|---|
| `bool SupportsAudio()` | Whether the webcam exposes a microphone. |
| `void SetAudioNodeID(int32 id)` | `-1` = auto, `0` = no audio, `>0` = a specific Media Kit node id. |

Statistics (poll any time during capture):

| Method | Purpose |
|---|---|
| `uint32 FramesCaptured()` | Frames delivered since start. |
| `uint32 FramesDropped()` | Frames dropped because the consumer fell behind. |
| `float CurrentFPS()` | Rolling frame rate. |

Identification (USB / driver), useful for UI and diagnostics:
`Name()`, `VendorID()`, `ProductID()`, `ProductName()`, `SerialNumber()`,
`DriverName()`, `DriverVersion()`, and `GetUSBInfo()` / `GetDriverInfo()` for the
full structs.

### Frame message contract

`StartCapture` posts, for every frame, to your looper:

- `what`: `'frcv'` (`MSG_FRAME_RECEIVED`)
- field `"bitmap"`: a `BBitmap*` (`FindPointer`), `B_RGB32`, **borrowed** — valid
  only during the message, owned by the library.

Audio level messages (when audio is active):

- `what`: `'audl'` (`MSG_WEBCAM_AUDIO_LEVEL`)
- fields `"left"` / `"right"`: `float` peak levels in `0.0 .. 1.0`.

> The defaults are `MSG_WEBCAM_FRAME` (`'frcv'`) and `MSG_WEBCAM_AUDIO_LEVEL`
> (`'audl'`), defined in `WebcamDevice.h` and owned by the library. Override
> them per-capture via the `StartCapture` parameters above — no need to touch
> the library source.

---

## Threading and ownership — the rules that bite

- **Bitmaps are borrowed.** Valid only inside the `'frcv'` message. Copy to keep.
  Never `delete`.
- **Devices are owned by the roster.** Do not delete `WebcamDevice*`; keep the
  `WebcamRoster` alive for as long as you use its devices.
- **`StopCapture()` is synchronous and thread-safe.** After it returns, no more
  frame messages will be posted for that device.
- **One looper, one handler thread.** Your frame handler must not block. Offload
  encoding/IO to your own thread; the library will keep capturing and simply
  drop frames you can't keep up with.

---

## Building

The classes are plain C++ with the Haiku Media Kit. To use them today, add the
sources to your build and link the system libraries:

```
SRCS  += WebcamRoster.cpp WebcamDevice.cpp VideoConsumer.cpp \
         AudioConsumer.cpp USBVideoParser.cpp
LIBS  += be media tracker translation device shared jpeg
```

(When packaged as a shared/static library, link `libbubicapture` instead and add
its headers to your include path. The contract above is unchanged.)

---

## License

MIT, same as BubiCam. See `LICENSE`.
