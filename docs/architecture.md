# phoneCtrl – Architecture

## System Overview

```
┌──────────────────────────────────────────────────────────────┐
│                         LAPTOP (host)                        │
│                                                              │
│  ┌───────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │ScreenCapture  │───▶│ VideoEncoder │───▶│WebSocketSrv  │  │
│  │ (DXGI)        │    │ (FFmpeg/H264)│    │              │  │
│  └───────────────┘    └──────────────┘    │  port 8080   │  │
│                                           │              │  │
│  ┌───────────────┐                        │              │  │
│  │ InputHandler  │◀───────────────────────│              │  │
│  │ (SendInput)   │    JSON events         └──────────────┘  │
│  └───────────────┘                               ▲ ▼        │
└─────────────────────────────────────────────────────────────┘
                                               WebSocket
                                         (binary video frames +
                                          JSON input events)
┌─────────────────────────────────────────────────────────────┐
│                         PHONE (client)                       │
│                                                              │
│  ┌───────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │ WebRtcService │───▶│RTCVideoView  │    │TouchOverlay  │  │
│  │ (flutter_webrtc│    │(H264 decode) │    │              │  │
│  └───────────────┘    └──────────────┘    └──────┬───────┘  │
│          ▲                                        │          │
│          │                               ┌──────▼───────┐   │
│          │ JSON events                   │ InputService  │   │
│          └───────────────────────────────│               │   │
│                                          └───────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Components

### Laptop Host (C++)

| Component | File | Purpose |
|-----------|------|---------|
| `ScreenCapture` | `capture/screen_capture.*` | DXGI Desktop Duplication — captures the primary monitor at 60 FPS using the GPU. Produces raw BGRA frames. |
| `VideoEncoder`  | `encoder/video_encoder.*`  | FFmpeg/libx264 H.264 encoder. Converts BGRA→YUV420p, encodes, and emits NAL unit packets. |
| `WebSocketServer` | `network/websocket_server.*` | Boost.Beast WebSocket server. Broadcasts encoded video frames and receives input JSON events. |
| `InputHandler` | `input/input_handler.*` | Parses JSON input events and calls `SendInput()` to inject mouse/keyboard events into Windows. |
| `main.cpp` | `src/main.cpp` | Wires all components together; handles CLI arguments and graceful shutdown. |

### Phone Client (Flutter)

| Component | File | Purpose |
|-----------|------|---------|
| `WebRtcService` | `services/webrtc_service.dart` | Manages WebRTC peer connection, SDP negotiation, and data channel for input. |
| `InputService` | `services/input_service.dart` | Translates touch coordinates to laptop coordinates and sends JSON events via WebRTC data channel. |
| `TouchOverlay` | `widgets/touch_overlay.dart` | Transparent gesture detector: single tap → click, long press → right-click, two-finger drag → scroll. |
| `RemoteDesktopScreen` | `screens/remote_desktop_screen.dart` | Main view: full-screen video + touch overlay + toolbar. |

### Signaling Server (Node.js)

| File | Purpose |
|------|---------|
| `signaling/server.js` | WebSocket signaling server that brokers SDP offers/answers and ICE candidates between laptop and phone. |

---

## Data Flow

### Video Streaming

```
1. DXGI acquires new desktop frame (BGRA, ~8 MB)
2. FFmpeg converts BGRA → YUV420p
3. libx264 encodes → H.264 NAL unit(s) (~10–50 KB at 4 Mbps)
4. WebSocketServer prefixes a 1-byte header (0x01=P-frame, 0x02=keyframe)
5. Binary frame is broadcast to all connected phone clients
6. Flutter WebRTC / MediaCodec decodes and renders
```

### Input Events

```
1. User touches phone screen
2. TouchOverlay captures gesture
3. InputService maps view → laptop coordinates
4. JSON event sent via WebRTC data channel
        { "type": "mousemove", "x": 532, "y": 211 }
5. WebSocketServer receives event on laptop
6. InputHandler parses JSON
7. SendInput() injects mouse/keyboard event into Windows
```

---

## Network

| Path | Protocol | Purpose |
|------|----------|---------|
| Phone → Signaling server | WebSocket (TCP) | SDP offer + ICE candidates |
| Laptop → Signaling server | WebSocket (TCP) | SDP answer + ICE candidates |
| Phone ↔ Laptop | WebRTC (DTLS-SRTP) | Video stream (laptop→phone) + input events (phone→laptop) |

### Ports

| Port | Service |
|------|---------|
| 3000 | Signaling server (WebSocket) |
| 8080 | Laptop host WebSocket server (video + input) |

---

## Performance Targets

| Metric | Target |
|--------|--------|
| Frame rate | 30 FPS (configurable up to 60) |
| Video bitrate | 4 Mbps (configurable) |
| Input latency | < 50 ms (LAN) |
| Codec | H.264 baseline, ultrafast preset, zerolatency tune |
| Screen capture | DXGI Desktop Duplication (GPU-backed, < 1 ms overhead) |

---

## Security

- All WebRTC media and data channel traffic is encrypted with **DTLS-SRTP**.
- The signaling server is intended to run on a **trusted local network**.  
  For internet use, add TLS to the signaling WebSocket (`wss://`) and  
  authenticate clients with a shared secret or token before brokering SDP.
- `SendInput()` runs in the user session with the privileges of the host  
  process — ensure the host is not exposed to untrusted networks.
