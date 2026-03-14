/**
 * phoneCtrl – Laptop Host (main.cpp)
 *
 * Entry point for the laptop-side application.
 *
 * Pipeline:
 *   ScreenCapture → VideoEncoder → WebSocketServer (→ phone)
 *   Phone (→ WebSocketServer) → InputHandler → SendInput
 *
 * Usage:
 *   phoneCtrl_host [--port <port>] [--fps <fps>] [--bitrate <bps>]
 *                  [--monitor <index>] [--width <w>] [--height <h>]
 */

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "capture/screen_capture.h"
#include "encoder/video_encoder.h"
#include "network/websocket_server.h"
#include "input/input_handler.h"

// ─── Graceful shutdown ────────────────────────────────────────────────────────

namespace {
std::atomic_bool g_running{true};
void signalHandler(int) { g_running = false; }
} // namespace

// ─── Simple CLI argument parser ───────────────────────────────────────────────

struct Config {
    uint16_t port     = 8080;
    int      fps      = 30;
    int64_t  bitrate  = 4'000'000; // 4 Mbps
    int      monitor  = 0;
};

static Config parseArgs(int argc, char* argv[])
{
    Config cfg;
    for (int i = 1; i < argc - 1; ++i) {
        std::string flag = argv[i];
        std::string val  = argv[i + 1];
        try {
            if (flag == "--port") {
                int p = std::stoi(val);
                if (p < 1 || p > 65535)
                    throw std::out_of_range("port must be 1-65535");
                cfg.port = static_cast<uint16_t>(p);
                ++i;
            } else if (flag == "--fps") {
                cfg.fps = std::stoi(val);
                ++i;
            } else if (flag == "--bitrate") {
                cfg.bitrate = std::stoll(val);
                ++i;
            } else if (flag == "--monitor") {
                cfg.monitor = std::stoi(val);
                ++i;
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid value for " << flag << ": " << e.what() << "\n";
            std::exit(EXIT_FAILURE);
        }
    }
    return cfg;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    const Config cfg = parseArgs(argc, argv);

    std::cout << "phoneCtrl host starting\n"
              << "  port     : " << cfg.port    << "\n"
              << "  fps      : " << cfg.fps     << "\n"
              << "  bitrate  : " << cfg.bitrate << " bps\n"
              << "  monitor  : " << cfg.monitor << "\n\n";

    // ── Input handler ────────────────────────────────────────────────────────
    phonectrl::input::InputHandler inputHandler;

    // ── WebSocket server ─────────────────────────────────────────────────────
    phonectrl::network::WebSocketServer server;

    server.setConnectCallback([](phonectrl::network::ClientId id) {
        std::cout << "[server] client connected  id=" << id << "\n";
    });
    server.setDisconnectCallback([](phonectrl::network::ClientId id) {
        std::cout << "[server] client disconnected id=" << id << "\n";
    });
    server.setMessageCallback([&inputHandler](const phonectrl::network::Message& msg) {
        // Input events arrive as UTF-8 JSON text from the phone
        if (!msg.isBinary && !msg.payload.empty()) {
            const std::string json(msg.payload.begin(), msg.payload.end());
            inputHandler.handleJson(json);
        }
    });

    if (!server.start(cfg.port)) {
        std::cerr << "Failed to start WebSocket server on port " << cfg.port << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "[server] Listening on ws://0.0.0.0:" << cfg.port << "\n";

    // ── Video encoder ────────────────────────────────────────────────────────
    phonectrl::encoder::VideoEncoder encoder;

    encoder.setPacketCallback([&server](const phonectrl::encoder::EncodedPacket& pkt) {
        // Add a 1-byte header: 0x01 = video frame, 0x02 = keyframe
        std::vector<uint8_t> msg;
        msg.reserve(1 + pkt.data.size());
        msg.push_back(pkt.keyframe ? 0x02 : 0x01);
        msg.insert(msg.end(), pkt.data.begin(), pkt.data.end());
        server.broadcast(msg);
    });

    // ── Screen capture ────────────────────────────────────────────────────────
    phonectrl::capture::ScreenCapture capture;

    capture.setFrameCallback([&](const phonectrl::capture::Frame& frame) {
        // Lazy-init encoder once we know the real screen dimensions
        if (!encoder.isInitialised()) {
            if (!encoder.init(frame.width, frame.height, cfg.fps, cfg.bitrate)) {
                std::cerr << "[encoder] Initialisation failed\n";
                return;
            }
            // Tell the input handler the screen resolution for coordinate mapping
            inputHandler.setScreenDimensions(frame.width, frame.height);
            std::cout << "[encoder] Initialised: "
                      << frame.width << "x" << frame.height
                      << " @ " << cfg.fps << " fps\n";
        }

        // Only encode when at least one client is connected
        if (server.clientCount() > 0) {
            encoder.encodeFrame(frame.data.data(),
                                frame.width, frame.height, frame.stride);
        }
    });

    if (!capture.start(cfg.monitor)) {
        std::cerr << "Failed to start screen capture (DXGI). "
                     "Ensure you are running on Windows with a connected display.\n";
        server.stop();
        return EXIT_FAILURE;
    }
    std::cout << "[capture] DXGI Desktop Duplication started on monitor "
              << cfg.monitor << "\n";

    // ── Run until Ctrl-C ─────────────────────────────────────────────────────
    std::cout << "\nPress Ctrl-C to stop.\n";
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ── Graceful shutdown ─────────────────────────────────────────────────────
    std::cout << "\nShutting down…\n";
    capture.stop();
    encoder.flush();
    server.stop();

    std::cout << "Done.\n";
    return EXIT_SUCCESS;
}
