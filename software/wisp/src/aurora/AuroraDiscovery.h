#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct mdns_search_once_s;

// Non-blocking lookup of the first `_aurora._tcp` service on the LAN. Drive
// from a loop: start() kicks off an async mDNS query, poll() advances it
// without ever blocking (true once the search finishes), cancel() aborts an
// in-flight query. Requires WiFi connected.
class AuroraDiscovery {
public:
    bool start(uint32_t timeoutMs = 3000);
    // True when the in-flight query has finished; then found() reports the
    // outcome. False (no block) while still pending or when none is in flight.
    bool poll();
    // Abandon the in-flight query. The mdns API can't free an unfinished
    // search, so the handle is retained and reaped by drain() once it ends.
    void cancel();
    // Non-blocking reap of an abandoned handle; call every tick. No-op unless
    // cancel() left a search draining.
    void drain();
    bool inFlight() const { return search_ != nullptr && !draining_; }
    IPAddress ip() const { return ip_; }
    uint16_t port() const { return port_; }
    bool found() const { return found_; }

private:
    mdns_search_once_s* search_ = nullptr;
    IPAddress ip_;
    uint16_t port_ = 0;
    bool found_ = false;
    bool draining_ = false;
    bool begun_ = false;   // MDNS.begin() is one-shot
};
