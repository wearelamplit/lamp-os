#include "AuroraDiscovery.h"
#include <ESPmDNS.h>

bool AuroraDiscovery::discover(uint32_t /*timeoutMs*/) {
    found_ = false;
    // MDNS.begin() is one-shot; re-calling it without end() re-inits the
    // responder and can fail or leak. Begin once, then query per call.
    if (!begun_) {
        if (!MDNS.begin("aurora-esp32-client")) {
            Serial.println("[mdns] MDNS.begin failed");
            return false;
        }
        begun_ = true;
    }
    // One query per call (queryService blocks for its own internal timeout).
    // The caller re-invokes from loop() until a service is found, so this
    // never wedges the state machine in a long blocking wait.
    int n = MDNS.queryService("aurora", "tcp");  // _aurora._tcp
    if (n > 0) {
        // ESP32 Arduino core 3.x (used by the C6 platform) renamed MDNS.IP() to
        // MDNS.address(). The reference targets core 2.x. Same semantics.
        ip_ = MDNS.address(0);
        port_ = MDNS.port(0);
        found_ = (port_ != 0);
        if (found_) {
            Serial.printf("[mdns] found aurora at %s:%u\n",
                          ip_.toString().c_str(), port_);
        }
        return found_;
    }
    return false;
}
