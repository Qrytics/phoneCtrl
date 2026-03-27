# phoneCtrl

**phoneCtrl** lets you use your phone as a remote control for your laptop —
like a pocket TeamViewer. Your phone displays the live laptop screen and
your touch gestures are translated into mouse and keyboard events.

---

## Architecture

```
Laptop ──────── WebSocket / WebRTC ──────── Phone
   │                                           │
   ├─ DXGI screen capture (GPU)                ├─ Video decode (H.264)
   ├─ FFmpeg H.264 encoder                     ├─ RTCVideoView (full-screen)
   ├─ WebSocket server                         ├─ Touch → mouse/keyboard
   └─ SendInput (mouse & keyboard)             └─ WebRTC data channel
```

See [`docs/architecture.md`](docs/architecture.md) for the full design.

---

## Repository Layout

```
phoneCtrl/
├── laptop/                   # C++ host application (Windows)
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       ├── capture/          # DXGI screen capture
│       │   ├── screen_capture.h
│       │   └── screen_capture.cpp
│       ├── encoder/          # FFmpeg / libx264 H.264 encoder
│       │   ├── video_encoder.h
│       │   └── video_encoder.cpp
│       ├── network/          # Boost.Beast WebSocket server
│       │   ├── websocket_server.h
│       │   └── websocket_server.cpp
│       └── input/            # Windows SendInput injection
│           ├── input_handler.h
│           └── input_handler.cpp
│
├── phone/                    # Flutter client (Android / iOS)
│   ├── pubspec.yaml
│   └── lib/
│       ├── main.dart
│       ├── screens/
│       │   └── remote_desktop_screen.dart
│       ├── services/
│       │   ├── webrtc_service.dart
│       │   └── input_service.dart
│       └── widgets/
│           └── touch_overlay.dart
│
├── signaling/                # Node.js WebRTC signaling server
│   ├── package.json
│   └── server.js
│
└── docs/
    └── architecture.md
```

---

## Prerequisites

### Laptop (host)

| Requirement | Version |
|-------------|---------|
| Windows 10 / 11 | 64-bit, DXGI 1.2+ |
| Visual Studio 2022 or Clang/MSVC | C++20 |
| CMake | ≥ 3.20 |
| [vcpkg](https://vcpkg.io) | (recommended for dependencies) |
| FFmpeg (libavcodec, libswscale, libavutil) | ≥ 6.0 |
| Boost | ≥ 1.81 (Beast + Asio) |

### Phone (client)

| Requirement | Version |
|-------------|---------|
| Flutter SDK | ≥ 3.0 |
| Dart SDK | ≥ 3.0 |
| Android 9+ or iOS 14+ | |

### Signaling Server

| Requirement | Version |
|-------------|---------|
| Node.js | ≥ 18 |

---

## Quick Start

### 1 — Start the Signaling Server

```bash
cd signaling
npm install
npm start
# Listening on ws://0.0.0.0:3000
```

### 2 — Build and Run the Laptop Host

#### Install dependencies with vcpkg

```powershell
# From a Developer PowerShell for VS 2022
vcpkg install ffmpeg[x264] boost-beast boost-asio
```

#### Configure and build

```powershell
cd laptop
# Replace <vcpkg-root> with your actual install path, for example:
#   C:/src/vcpkg/scripts/buildsystems/vcpkg.cmake
# or (if VCPKG_ROOT is set):
#   $env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

If CMake says it cannot find the toolchain file, verify `VCPKG_ROOT` first:

```powershell
echo $env:VCPKG_ROOT
Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

If `VCPKG_ROOT` is empty, set it for the current shell:

```powershell
$env:VCPKG_ROOT="C:/src/vcpkg"  # change to your actual vcpkg path
```

If the host starts but prints `[encoder] Initialisation failed`, your FFmpeg
build likely lacks an H.264 encoder (`libx264` or `libopenh264`). Ensure you
installed `ffmpeg[x264]` (shown above), then rebuild:

```powershell
vcpkg install ffmpeg[x264]
cmake -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

#### Run

```powershell
.\build\Release\phoneCtrl_host.exe --port 8080 --fps 30 --bitrate 4000000
```

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | 8080 | WebSocket port the phone connects to |
| `--fps` | 30 | Target capture / encode frame rate |
| `--bitrate` | 4000000 | H.264 bit-rate in bps (e.g. 4 Mbps) |
| `--monitor` | 0 | 0-based index of the monitor to capture |

### 3 — Run the Phone App

```bash
cd phone
flutter --version
flutter pub get
flutter run
```

If `flutter` is not recognized on Windows, install Flutter SDK and add
`<flutter-sdk>\bin` to your `PATH`, then open a new terminal and rerun
`flutter --version`.

Enter your **laptop's local IP address** and port `8080` on the connect screen.

---

## Gesture Reference

| Gesture | Action |
|---------|--------|
| Single tap | Left click |
| Long press | Right click |
| Single-finger drag | Mouse move |
| Two-finger vertical drag | Scroll |
| Double-tap (anywhere) | Show / hide toolbar |

---

## Input Event Protocol

Input events are sent as JSON text messages over the WebRTC data channel
(or WebSocket if using the direct mode):

| Event | JSON |
|-------|------|
| Mouse move | `{ "type": "mousemove", "x": 532, "y": 211 }` |
| Left click | `{ "type": "click", "x": 532, "y": 211, "button": "left" }` |
| Right click | `{ "type": "click", "x": 532, "y": 211, "button": "right" }` |
| Scroll | `{ "type": "scroll", "dx": 0, "dy": 3 }` |
| Key press | `{ "type": "keypress", "key": "0x41" }` |
| Key down | `{ "type": "keydown", "key": "0x41" }` |
| Key up | `{ "type": "keyup",   "key": "0x41" }` |

`x` / `y` are in laptop screen pixels. `key` is a Windows Virtual-Key code.

---

## Performance Tips

- Use a **wired Ethernet** connection on the laptop side for lowest latency.
- Increase `--fps` to 60 on fast hardware (requires a strong Wi-Fi or LAN link).
- Lower `--bitrate` (e.g. 2000000) if you observe video stuttering on Wi-Fi.
- Only changed regions are encoded efficiently by H.264's inter-prediction —
  still/idle screens use almost no bandwidth.

---

## Security Notes

- All WebRTC media and data channel traffic is **DTLS-SRTP encrypted**.
- The signaling server and laptop host are designed for **trusted local networks**.
- For remote / internet access, place the signaling server behind a TLS reverse
  proxy (e.g. nginx with `wss://`) and add token-based authentication before
  forwarding SDP messages.
- The laptop host process calls `SendInput()` with the current user's privileges;
  never expose it to the public internet without authentication.

---

## License

MIT — see [LICENSE](LICENSE) for details.
