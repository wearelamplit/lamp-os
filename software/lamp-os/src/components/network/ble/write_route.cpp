#include "write_route.hpp"

#ifdef LAMP_DEBUG
#include <Arduino.h>
#endif

namespace ble_control {

const char* WriteRoute::deliver(const std::string& raw, uint16_t handle) const {
  if (decode_) {
    // The 64-byte slop above maxSize_ covers GCM prefix+tag overhead on the
    // raw wire bytes.
    if (raw.size() > maxSize_ + 64) return "oversize";
    std::string json;
    bool authed = false;
    if (!decode_(raw, handle, uuidSalt_, shortName_, json, authed)) return "decode failed";
    if (!authed) return "unauthenticated";
    if (json.size() > maxSize_) return "oversize";
    if (!allowEmpty_ && json.empty()) return "empty";
#ifdef LAMP_DEBUG
    if (debugTag_) {
      Serial.printf("[ble_control] WRITE %s len=%u (decoded)\n",
                    debugTag_, (unsigned)json.size());
    }
#endif
    post_(json.data(), json.size());
    return nullptr;
  }

  if (!auth_(handle)) return "unauthenticated";
  if (raw.size() > maxSize_) return "oversize";
  if (!allowEmpty_ && raw.empty()) return "empty";
#ifdef LAMP_DEBUG
  if (debugTag_) {
    Serial.printf("[ble_control] WRITE %s len=%u\n",
                  debugTag_, (unsigned)raw.size());
  }
#endif
  post_(raw.data(), raw.size());
  return nullptr;
}

}  // namespace ble_control
