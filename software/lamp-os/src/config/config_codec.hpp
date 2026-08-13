#pragma once

#include <ArduinoJson.h>

#include "config_types.hpp"

// JSON codec for the persisted "cfg" blob. Pure functions over the config
// model structs (no NVS, no Serial), so the parse/serialize logic runs in
// the native suite (round-trip tested in test/test_config_codec). Config owns
// the store, the section cache, and logging; the byte shape lives here.
namespace lamp {
namespace config_codec {

// Parse the "cfg" blob into the model. Applies the field defaults, clamps,
// byte-order derivation, and legacy migrations the loader relies on. Leaves
// each struct at its class default for any absent key.
void fromJson(JsonObject root, LampSettings& lamp, BaseSettings& base,
              ShadeSettings& shade, ExpressionSettings& expressions,
              HomeModeSettings& homeMode);

// Serialize the model into root (the canonical "cfg" blob shape, also what
// the constructor re-parses on the next boot).
void toJson(JsonObject root, const LampSettings& lamp, const BaseSettings& base,
            const ShadeSettings& shade, const ExpressionSettings& expressions,
            const HomeModeSettings& homeMode);

// True when `json` is a whole config document safe to persist: it parses and
// carries a `lamp` object. persistRawJson gates on this so a truncated or
// unparseable blob (Arduino String silently truncates on a fragmented-heap
// OOM) can't overwrite a good stored config. A parse OOM returns non-Ok, so
// it also rejects rather than persisting a partial write.
bool configBlobPersistable(const char* json);

}  // namespace config_codec
}  // namespace lamp
