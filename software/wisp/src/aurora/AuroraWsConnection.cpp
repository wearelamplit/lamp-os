#include "AuroraWsConnection.h"
#include <algorithm>

using namespace websockets;

void AuroraWsConnection::tryConnect() {
    if (port_ == 0) return;
    lastAttemptMs_ = millis();

    // Bind handlers once — re-binding on every attempt is wasteful and can
    // stack callbacks.
    if (!handlersBound_) {
        client_.onMessage([this](WebsocketsMessage msg) {
            if (msg.isBinary()) {
                const auto& d = msg.rawData();
                if (onBinary_) onBinary_((const uint8_t*)d.c_str(), d.length());
            }
        });
        client_.onEvent([this](WebsocketsEvent ev, String) {
            if (ev == WebsocketsEvent::ConnectionClosed) connected_ = false;
        });
        handlersBound_ = true;
    }

    String url = "ws://" + ip_.toString() + ":" + String(port_) + "/notifications";
    Serial.printf("[ws] connecting %s\n", url.c_str());
    connected_ = client_.connect(url);
    if (connected_) {
        backoffMs_ = 500;
        consecutiveFailures_ = 0;
        lastPingMs_ = millis();
        if (onOpen_) onOpen_();
    } else {
        consecutiveFailures_++;
        backoffMs_ = std::min<uint32_t>(backoffMs_ * 2, kMaxBackoffMs);
    }
}

bool AuroraWsConnection::send(const uint8_t* data, size_t len) {
    if (!connected_) return false;
    return client_.sendBinary((const char*)data, len);
}

void AuroraWsConnection::loop() {
    if (connected_) {
        client_.poll();
        uint32_t now = millis();
        // Periodic ping keeps the TCP connection alive and surfaces dead peers
        // (the device may send no app data for long stretches when idle).
        if (now - lastPingMs_ >= kPingIntervalMs) {
            lastPingMs_ = now;
            client_.ping();
        }
        if (!client_.available()) {
            Serial.println("[ws] connection lost");
            connected_ = false;
            lastAttemptMs_ = now;
            consecutiveFailures_++;
        }
        return;
    }
    if (millis() - lastAttemptMs_ >= backoffMs_) tryConnect();
}
