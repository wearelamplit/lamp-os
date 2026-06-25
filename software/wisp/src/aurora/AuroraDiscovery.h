#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Wraps ESPmDNS to find the first `_aurora._tcp` service on the LAN.
class AuroraDiscovery {
public:
    // Requires WiFi connected. Returns true if a service was found.
    bool discover(uint32_t timeoutMs = 5000);
    IPAddress ip() const { return ip_; }
    uint16_t port() const { return port_; }
    bool found() const { return found_; }

private:
    IPAddress ip_;
    uint16_t port_ = 0;
    bool found_ = false;
    bool begun_ = false;   // MDNS.begin() is one-shot
};
