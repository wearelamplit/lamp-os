#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <ArduinoWebsockets.h>
#include <functional>

// Maintains the binary notification WebSocket with reconnect + ping keepalive.
class AuroraWsConnection {
public:
    using BinaryHandler = std::function<void(const uint8_t*, size_t)>;
    using OpenHandler   = std::function<void()>;

    void onBinary(BinaryHandler h) { onBinary_ = std::move(h); }
    void onOpen(OpenHandler h)     { onOpen_ = std::move(h); }

    void setTarget(IPAddress ip, uint16_t port) { ip_ = ip; port_ = port; }
    void loop();                 // call frequently
    bool isConnected() const { return connected_; }
    bool send(const uint8_t* data, size_t len);
    // Consecutive failed connects / drops since the last successful open.
    uint32_t consecutiveFailures() const { return consecutiveFailures_; }

private:
    void tryConnect();

    websockets::WebsocketsClient client_;
    IPAddress ip_;
    uint16_t port_ = 0;
    bool connected_ = false;
    bool handlersBound_ = false;       // bind onMessage/onEvent only once
    uint32_t lastPingMs_ = 0;
    uint32_t lastAttemptMs_ = 0;
    uint32_t backoffMs_ = 500;
    uint32_t consecutiveFailures_ = 0;
    BinaryHandler onBinary_;
    OpenHandler onOpen_;

    // PALETTE_STATE/PATTERN_STATE are event-driven, so an idle device may send
    // no app data for a long time. We detect dead connections with a periodic
    // ping + WebsocketsClient::available() rather than a data-inactivity timer.
    static constexpr uint32_t kPingIntervalMs = 10000;
    static constexpr uint32_t kMaxBackoffMs   = 8000;
};
