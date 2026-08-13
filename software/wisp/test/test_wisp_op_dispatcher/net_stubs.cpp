// Minimal stub implementations of WifiLink and StageBeacon for native tests.
// The dispatcher tests never exercise the setWifi path (wifiLink_ == nullptr),
// so these bodies are unreachable but are required to satisfy the linker.
#include "net/wifi_link.hpp"
#include "net/stage_beacon.hpp"

namespace wisp {

void WifiLink::begin(WispConfig*) {}
void WifiLink::reconnect() {}
void WifiLink::startSoftAp(const char*, const char*) {}
bool WifiLink::canBroadcast() const { return false; }
size_t WifiLink::apClientIps(IPAddress*, size_t) const { return 0; }
bool WifiLink::isConnected() const { return false; }
std::string WifiLink::ssid() const { return {}; }
std::string WifiLink::password() const { return {}; }

void StageBeacon::begin(const std::string&, WispConfig*) {}
void StageBeacon::refreshAdvert() {}
void StageBeacon::advertiseCreds(const std::string&, const std::string&) {}
void StageBeacon::stop() {}
void StageBeacon::startAdvert(const std::string&, const std::string&) {}

}  // namespace wisp
