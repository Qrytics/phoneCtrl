#include "websocket_server.h"

#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

// Boost.Beast / Boost.Asio WebSocket server
// Headers are included via the CMake target boost::beast
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
using tcp           = net::ip::tcp;

namespace phonectrl {
namespace network {

// ─── Session ─────────────────────────────────────────────────────────────────

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, ClientId id,
            MessageCallback    onMsg,
            DisconnectCallback onDisc)
        : ws_(std::move(socket))
        , id_(id)
        , onMsg_(std::move(onMsg))
        , onDisc_(std::move(onDisc))
    {
        // Default to binary mode; individual sends override this per-message
        // (video frames are binary, input acknowledgements may be text).
        ws_.binary(true);
    }

    void start()
    {
        ws_.async_accept(
            beast::bind_front_handler(&Session::onAccept, shared_from_this()));
    }

    void send(const std::vector<uint8_t>& data, bool binary = true)
    {
        auto buf = std::make_shared<std::vector<uint8_t>>(data);
        net::post(ws_.get_executor(),
            [self = shared_from_this(), buf, binary]() mutable {
                self->writeQueue_.push_back({ buf, binary });
                if (self->writeQueue_.size() == 1) {
                    self->doWrite();
                }
            });
    }

    void sendText(const std::string& text)
    {
        auto buf = std::make_shared<std::vector<uint8_t>>(text.begin(), text.end());
        net::post(ws_.get_executor(),
            [self = shared_from_this(), buf]() mutable {
                self->writeQueue_.push_back({ buf, false });
                if (self->writeQueue_.size() == 1) {
                    self->doWrite();
                }
            });
    }

    ClientId id() const { return id_; }

private:
    struct WriteItem {
        std::shared_ptr<std::vector<uint8_t>> data;
        bool binary;
    };

    void onAccept(beast::error_code ec)
    {
        if (ec) return;
        doRead();
    }

    void doRead()
    {
        ws_.async_read(buffer_,
            beast::bind_front_handler(&Session::onRead, shared_from_this()));
    }

    void onRead(beast::error_code ec, std::size_t /*bytesRead*/)
    {
        if (ec) {
            if (onDisc_) onDisc_(id_);
            return;
        }

        if (onMsg_) {
            Message msg;
            msg.clientId = id_;
            msg.isBinary = ws_.got_binary();
            auto bufData = buffer_.data();
            msg.payload.resize(bufData.size());
            std::memcpy(msg.payload.data(), bufData.data(), bufData.size());
            onMsg_(msg);
        }

        buffer_.clear();
        doRead();
    }

    void doWrite()
    {
        if (writeQueue_.empty()) return;
        auto& item = writeQueue_.front();
        ws_.binary(item.binary);
        ws_.async_write(
            net::buffer(*item.data),
            beast::bind_front_handler(&Session::onWrite, shared_from_this()));
    }

    void onWrite(beast::error_code ec, std::size_t /*bytes*/)
    {
        if (ec) {
            if (onDisc_) onDisc_(id_);
            return;
        }
        writeQueue_.erase(writeQueue_.begin());
        if (!writeQueue_.empty()) doWrite();
    }

    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    ClientId           id_;
    MessageCallback    onMsg_;
    DisconnectCallback onDisc_;
    std::vector<WriteItem> writeQueue_;
};

// ─── Listener ────────────────────────────────────────────────────────────────

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& ioc, tcp::endpoint endpoint,
             std::atomic<ClientId>& nextId,
             std::map<ClientId, std::shared_ptr<Session>>& sessions,
             std::mutex&          sessionsMutex,
             ConnectCallback      onConn,
             DisconnectCallback   onDisc,
             MessageCallback      onMsg)
        : ioc_(ioc)
        , acceptor_(net::make_strand(ioc))
        , nextId_(nextId)
        , sessions_(sessions)
        , sessionsMutex_(sessionsMutex)
        , onConn_(std::move(onConn))
        , onDisc_(std::move(onDisc))
        , onMsg_(std::move(onMsg))
    {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(net::socket_base::max_listen_connections);
    }

    void run() { doAccept(); }

private:
    void doAccept()
    {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(&Listener::onAccept, shared_from_this()));
    }

    void onAccept(beast::error_code ec, tcp::socket socket)
    {
        if (!ec) {
            ClientId id = nextId_++;
            auto session = std::make_shared<Session>(
                std::move(socket), id, onMsg_,
                [this, id](ClientId cid) {
                    {
                        std::lock_guard<std::mutex> lk(sessionsMutex_);
                        sessions_.erase(cid);
                    }
                    if (onDisc_) onDisc_(cid);
                });

            {
                std::lock_guard<std::mutex> lk(sessionsMutex_);
                sessions_[id] = session;
            }

            if (onConn_) onConn_(id);
            session->start();
        }
        doAccept();
    }

    net::io_context& ioc_;
    tcp::acceptor    acceptor_;

    std::atomic<ClientId>&  nextId_;
    std::map<ClientId, std::shared_ptr<Session>>& sessions_;
    std::mutex& sessionsMutex_;

    ConnectCallback    onConn_;
    DisconnectCallback onDisc_;
    MessageCallback    onMsg_;
};

// ─── WebSocketServer::Impl ────────────────────────────────────────────────────

struct WebSocketServer::Impl {
    ConnectCallback    onConnect;
    DisconnectCallback onDisconnect;
    MessageCallback    onMessage;

    net::io_context ioc{1};
    std::thread     ioThread;
    std::atomic_bool running{false};
    std::atomic<ClientId> nextId{1};

    std::map<ClientId, std::shared_ptr<Session>> sessions;
    std::mutex sessionsMutex;
};

// ─── Public WebSocketServer API ───────────────────────────────────────────────

WebSocketServer::WebSocketServer()
    : impl_(std::make_unique<Impl>())
{}

WebSocketServer::~WebSocketServer()
{
    stop();
}

void WebSocketServer::setConnectCallback(ConnectCallback cb)    { impl_->onConnect    = std::move(cb); }
void WebSocketServer::setDisconnectCallback(DisconnectCallback cb) { impl_->onDisconnect = std::move(cb); }
void WebSocketServer::setMessageCallback(MessageCallback cb)    { impl_->onMessage    = std::move(cb); }

bool WebSocketServer::start(uint16_t port)
{
    if (impl_->running.load()) return true;

    try {
        auto endpoint = tcp::endpoint(tcp::v4(), port);
        auto listener = std::make_shared<Listener>(
            impl_->ioc, endpoint,
            impl_->nextId,
            impl_->sessions,
            impl_->sessionsMutex,
            impl_->onConnect,
            impl_->onDisconnect,
            impl_->onMessage);
        listener->run();
    } catch (const std::exception& e) {
        std::cerr << "[WebSocketServer] start() failed: " << e.what() << "\n";
        return false;
    }

    impl_->running = true;
    impl_->ioThread = std::thread([this]{ impl_->ioc.run(); });
    return true;
}

void WebSocketServer::stop()
{
    impl_->running = false;
    impl_->ioc.stop();
    if (impl_->ioThread.joinable()) {
        impl_->ioThread.join();
    }
}

void WebSocketServer::broadcast(const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lk(impl_->sessionsMutex);
    for (auto& [id, session] : impl_->sessions) {
        session->send(data, /*binary=*/true);
    }
}

void WebSocketServer::sendTo(ClientId id, const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lk(impl_->sessionsMutex);
    auto it = impl_->sessions.find(id);
    if (it != impl_->sessions.end()) {
        it->second->send(data, /*binary=*/true);
    }
}

void WebSocketServer::sendTextTo(ClientId id, const std::string& text)
{
    std::lock_guard<std::mutex> lk(impl_->sessionsMutex);
    auto it = impl_->sessions.find(id);
    if (it != impl_->sessions.end()) {
        it->second->sendText(text);
    }
}

size_t WebSocketServer::clientCount() const
{
    std::lock_guard<std::mutex> lk(impl_->sessionsMutex);
    return impl_->sessions.size();
}

bool WebSocketServer::isRunning() const { return impl_->running.load(); }

} // namespace network
} // namespace phonectrl
