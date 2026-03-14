#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace phonectrl {
namespace network {

/// Opaque handle identifying a connected client.
using ClientId = uint64_t;

/// A raw binary or text message received from a client.
struct Message {
    ClientId    clientId;
    std::vector<uint8_t> payload;
    bool        isBinary = true;
};

/// Callbacks the server owner can register.
using ConnectCallback    = std::function<void(ClientId)>;
using DisconnectCallback = std::function<void(ClientId)>;
using MessageCallback    = std::function<void(const Message&)>;

/**
 * WebSocketServer
 *
 * A simple multi-client WebSocket server built on top of Boost.Beast +
 * Boost.Asio.  It runs its own I/O thread so callers don't need to drive
 * the event loop manually.
 *
 * This server is used for two purposes:
 *  1. Carrying encoded H.264 video frames to the phone (binary messages).
 *  2. Receiving mouse/keyboard input events from the phone (JSON text messages).
 *
 * Usage:
 *   WebSocketServer srv;
 *   srv.setConnectCallback([](ClientId id){ ... });
 *   srv.setMessageCallback([](const Message& m){ inputHandler.handle(m); });
 *   srv.start(8080);
 *   srv.broadcast(packetData);  // from encoder callback
 *   srv.stop();
 */
class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    // Non-copyable
    WebSocketServer(const WebSocketServer&)            = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    void setConnectCallback(ConnectCallback cb);
    void setDisconnectCallback(DisconnectCallback cb);
    void setMessageCallback(MessageCallback cb);

    /**
     * Start listening on the given port.
     * Spawns a background I/O thread.
     * @returns true if the server bound to the port successfully.
     */
    bool start(uint16_t port = 8080);

    /**
     * Stop accepting connections and close all active sessions.
     */
    void stop();

    /// Send a binary payload to all currently connected clients.
    void broadcast(const std::vector<uint8_t>& data);

    /// Send a binary payload to a specific client.
    void sendTo(ClientId id, const std::vector<uint8_t>& data);

    /// Send a UTF-8 text message to a specific client.
    void sendTextTo(ClientId id, const std::string& text);

    /// Returns the number of currently connected clients.
    size_t clientCount() const;

    bool isRunning() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace network
} // namespace phonectrl
