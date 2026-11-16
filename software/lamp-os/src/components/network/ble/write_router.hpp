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

#include <string>

#include "components/firmware/ota_quiet_mode.hpp"
#include "util/dropped_write_log.hpp"
#include "write_route.hpp"

namespace ble_control {

// Defined in ble_control.cpp. Stamps the last-write clock the Social-view
// duty-cycled scan reads to skip a scan window during active app writes.
void noteAppWriteActivity();

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
  WriteRouter(size_t maxSize, PostFn post, AuthFn auth) : route_(maxSize, post, auth) {}

  // Ciphertext flavor. decodeIncomingOp() runs before bounds-check.
  // maxSize bounds the DECODED JSON size (matches the slot capacity);
  // the over-the-wire bound is maxSize + 64 to leave headroom for the
  // GCM prefix + tag (matches the existing per-callback sanity ceiling).
  // `uuidSalt` must point to a 16-byte salt that outlives this object
  // (the BLE callbacks compute these once via uuidSaltLE() into a
  // function-local static array).
  WriteRouter(size_t maxSize, PostFn post, AuthFn auth, DecodeFn decode,
              const uint8_t* uuidSalt, const char* shortName)
      : route_(maxSize, post, auth, decode, uuidSalt, shortName) {}

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
    route_.setDebugTag(tag);
    return this;
  }

  // Optional: allow an empty payload to reach the post-helper. The default
  // is to reject empty writes (the original ShadeColorsCallback / etc.
  // dropped them). CHAR_EXPRESSION_TEST uses len==0 as a "test complete"
  // sentinel, so its router is constructed with allowEmpty(true).
  WriteRouter* setAllowEmpty(bool allow) {
    route_.setAllowEmpty(allow);
    return this;
  }

  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    const char* tag = route_.debugTag() ? route_.debugTag() : "write";
    // Silent-drop every live-control write while OTA is in progress.
    // The OTA characteristics (CHAR_FW_CONTROL / CHAR_FW_CHUNK) have
    // their own callbacks and bypass WriteRouter, so they keep working
    // for the BLE-pushed OTA chunk transport. Everything else (color,
    // shade, brightness, expression, settings, page) is gated here so
    // the phone can't fight the OTA for radio time / config state.
    if (lamp::ota_quiet_mode::isQuiet()) {
      lamp::logDroppedWrite(tag, "ota quiet");
      return;
    }
    noteAppWriteActivity();
    if (const char* why = route_.deliver(c->getValue(), info.getConnHandle())) {
      lamp::logDroppedWrite(tag, why);
    }
  }

 private:
  WriteRoute route_;
};

}  // namespace ble_control
