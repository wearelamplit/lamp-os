#pragma once

// WriteRouter is the single NimBLECharacteristicCallbacks subclass that handles
// the auth-gated, bounds-checked write path shared by most JSON-payload
// characteristics on the lamp's BLE control service. Replaces the bulk
// of the 11 near-identical Callback classes in ble_control.cpp.
//
// Two flavors are needed because some writes are plaintext (the BLE
// callback just bounds-checks raw byte count and posts) and some are
// AES-GCM ciphertext (the callback must decodeIncomingOp() first and
// then re-bounds-check the decoded JSON). Both flavors funnel through
// the same PostFn so the rest of lamp.cpp doesn't care which
// transport-layer envelope the JSON arrived in.
//
// Not consolidated into WriteRouter:
//   - BrightnessCallback         (single u8 value, not JSON)
//   - BaseKnockoutCallback       (two raw u8 bytes)
//   - HomeModeFocusCallback      (single u8 + flips a module-level flag)
//   - AuthCallback               (plaintext-vs-ciphertext auth bifurcation)
//   - WifiStateCallback          (read-only)
//   - LampSectionCallback etc.   (read-only)
//   - SettingsBlobCallback       (read+write, custom size budget, debug
//                                 logs the decrypt/auth failure path
//                                 separately; clarity > consolidation)
//   - SocialDispositionsCallback (read+write, with auth-gated read)
//
// Each of those carries enough idiomatic special-case that flattening
// them into WriteRouter would obscure intent. The 5 callbacks WriteRouter
// replaces are the ones whose entire body is "auth-check, size-check,
// post → drain on Core 1".

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "components/firmware/ota_quiet_mode.hpp"

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

// Per-characteristic write handler. Construct once at service-create time
// with the bounds + post function for the slot that owns this char's
// payload. The same class shape covers both plaintext and ciphertext
// paths via `needsDecode`. For decode-needed instances the constructor
// also takes the per-char salt (uuidSaltLE) + a short name used by the
// crypto layer's HKDF info-string.
class WriteRouter : public NimBLECharacteristicCallbacks {
 public:
  // Plaintext flavor. No AES-GCM decode. Used for live-preview chars
  // (CHAR_SHADE_COLORS, CHAR_BASE_COLORS) and the expression CRUD chars
  // (CHAR_EXPRESSION_OP, CHAR_EXPRESSION_TEST) where the app sends raw
  // JSON. maxSize bounds the bytes-on-wire; the same value is the
  // slot's capacity in lamp.cpp.
  WriteRouter(size_t maxSize, PostFn post, AuthFn auth)
      : maxSize_(maxSize),
        post_(post),
        auth_(auth),
        decode_(nullptr),
        uuidSalt_(nullptr),
        shortName_(nullptr),
        debugTag_(nullptr),
        allowEmpty_(false) {}

  // Ciphertext flavor. decodeIncomingOp() runs before bounds-check.
  // maxSize bounds the DECODED JSON size (matches the slot capacity);
  // the over-the-wire bound is maxSize + 64 to leave headroom for the
  // GCM prefix + tag (matches the existing per-callback sanity ceiling).
  // `uuidSalt` must point to a 16-byte salt that outlives this object
  // (the BLE callbacks compute these once via uuidSaltLE() into a
  // function-local static array).
  WriteRouter(size_t maxSize, PostFn post, AuthFn auth, DecodeFn decode,
              const uint8_t* uuidSalt, const char* shortName)
      : maxSize_(maxSize),
        post_(post),
        auth_(auth),
        decode_(decode),
        uuidSalt_(uuidSalt),
        shortName_(shortName),
        debugTag_(nullptr),
        allowEmpty_(false) {}

  // Optional: enable the [ble_control] WRITE <tag> len=... debug print
  // that the original callbacks emitted. The tag string must be a
  // process-lifetime literal (the BLE side typically passes a string
  // literal at construction time).
  //
  // Returns `this` so the call site can chain straight into
  // setCallbacks(...). NimBLE's setCallbacks takes a base-class pointer
  // and the chained pointer-returning setters avoid an intermediate
  // temporary.
  WriteRouter* setDebugTag(const char* tag) {
    debugTag_ = tag;
    return this;
  }

  // Optional: allow an empty payload to reach the post-helper. The default
  // is to reject empty writes (the original ShadeColorsCallback / etc.
  // dropped them). CHAR_EXPRESSION_TEST uses len==0 as a "test complete"
  // sentinel, so its router is constructed with allowEmpty(true).
  WriteRouter* setAllowEmpty(bool allow) {
    allowEmpty_ = allow;
    return this;
  }

  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    // Silent-drop every live-control write while OTA is in progress.
    // The OTA characteristics (CHAR_FW_CONTROL / CHAR_FW_CHUNK) have
    // their own callbacks and bypass WriteRouter, so they keep working
    // for the BLE-pushed OTA chunk transport. Everything else (color,
    // shade, brightness, expression, settings, page) is gated here so
    // the phone can't fight the OTA for radio time / config state.
    if (lamp::ota_quiet_mode::isQuiet()) return;

    const uint16_t handle = info.getConnHandle();
    const std::string raw = c->getValue();

    if (decode_) {
      // Ciphertext path. The 64-byte slop above maxSize_ covers GCM prefix+tag
      // overhead on the raw wire bytes. decode rejects > slot capacity after
      // decryption.
      if (raw.size() > maxSize_ + 64) return;
      std::string json;
      bool authed = false;
      if (!decode_(raw, handle, uuidSalt_, shortName_, json, authed)) return;
      if (!authed) return;
      if (json.size() > maxSize_) return;
      if (!allowEmpty_ && json.empty()) return;
#ifdef LAMP_DEBUG
      if (debugTag_) {
        Serial.printf("[ble_control] WRITE %s len=%u (decoded)\n",
                      debugTag_, (unsigned)json.size());
      }
#endif
      post_(json.data(), json.size());
      return;
    }

    // Plaintext path.
    if (!auth_(handle)) return;
    if (raw.size() > maxSize_) return;
    if (!allowEmpty_ && raw.empty()) return;
#ifdef LAMP_DEBUG
    if (debugTag_) {
      Serial.printf("[ble_control] WRITE %s len=%u\n",
                    debugTag_, (unsigned)raw.size());
    }
#endif
    post_(raw.data(), raw.size());
  }

 private:
  size_t      maxSize_;
  PostFn      post_;
  AuthFn      auth_;
  DecodeFn    decode_;
  const uint8_t* uuidSalt_;
  const char* shortName_;
  const char* debugTag_;
  bool        allowEmpty_;
};

}  // namespace ble_control
