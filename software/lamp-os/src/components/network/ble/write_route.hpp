#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ble_control {

// The post hop is the single bytes-into-pending-slot call defined in
// lamp.cpp. WriteRouter does not own the slot; it just forwards.
using PostFn = void (*)(const char* data, size_t len);

// AuthFn is the connection-handle auth check. ble_control.cpp defines a
// `static bool isAuthed(uint16_t)`; WriteRouter takes a function pointer so
// it doesn't depend on file-private linkage. The cost of the indirect call
// is negligible vs. the BLE write path's existing memcpy.
using AuthFn = bool (*)(uint16_t connHandle);

// Decoder for the AES-GCM ciphertext path. Same signature as
// decodeIncomingOp() in ble_control.cpp. When WriteRouter is instantiated
// with needsDecode = true, it calls this on every write and replaces
// `raw` with the decoded JSON before bounds-checking and posting.
using DecodeFn = bool (*)(const std::string& raw,
                          uint16_t handle,
                          const uint8_t* charUuidLE16,
                          const char* charShortName,
                          std::string& outJson,
                          bool& authed);

// The transport-free half of a routed write: the size, auth and decode checks
// and the post. A WriteRouter owns one per characteristic.
class WriteRoute {
 public:
  WriteRoute(size_t maxSize, PostFn post, AuthFn auth, DecodeFn decode = nullptr,
             const uint8_t* uuidSalt = nullptr, const char* shortName = nullptr)
      : maxSize_(maxSize),
        post_(post),
        auth_(auth),
        decode_(decode),
        uuidSalt_(uuidSalt),
        shortName_(shortName) {}

  // The tag the [ble_control] WRITE debug line and the [drop] line name. A
  // process-lifetime literal.
  void setDebugTag(const char* tag) { debugTag_ = tag; }
  const char* debugTag() const { return debugTag_; }

  void setAllowEmpty(bool allow) { allowEmpty_ = allow; }

  // Posts the write from `handle`, or returns why it was refused (a literal)
  // without posting.
  const char* deliver(const std::string& raw, uint16_t handle) const;

 private:
  size_t         maxSize_;
  PostFn         post_;
  AuthFn         auth_;
  DecodeFn       decode_;
  const uint8_t* uuidSalt_;
  const char*    shortName_;
  const char*    debugTag_   = nullptr;
  bool           allowEmpty_ = false;
};

}  // namespace ble_control
