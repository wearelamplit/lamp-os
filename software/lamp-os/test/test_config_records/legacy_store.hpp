#pragma once

// The save path releases up to 1.3.3 ran, frozen, so tests can build every
// store those releases leave in the wild: a whole "cfg" string, a split store
// ("cfg" lamp section, "cfgb"/"cfgc" rest, "ko", records), a whole config
// staged as a rest blob, and every store a save cut short leaves between.
// Do not change it to follow the codec: it is what shipped.

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "config/config_codec.hpp"
#include "config/config_store.hpp"
#include "util/json_writer.hpp"

namespace lamp {
namespace legacy133 {

using config_codec::SaveResult;

constexpr size_t kMaxStoredCfgJsonLen = 1824;
constexpr size_t kMaxWholeCfgLen = 1759;

namespace detail {

inline const char* rotationDirName(RotationDir dir) {
  return dir == RotationDir::Down ? "down" : "up";
}
// A role stores `rotate` only with one segment and a rotation off its default,
// so a lamp that never touches rotation writes what it always has.
template <class Role>
inline bool storesRotation(const Role& role) {
  return role.rotatable() && role.rotation != Role::kDefaultRotation;
}
inline void writeRotateJson(JsonWriter& w, const ColorRotation& rotation) {
  w.key("rotate");
  w.beginObject();
  w.key("on");
  w.value(rotation.on);
  w.key("periodS");
  w.value(static_cast<uint32_t>(rotation.periodS));
  w.key("dir");
  w.value(rotationDirName(rotation.dir));
  w.endObject();
}
inline void writeSegmentsJson(JsonWriter& w, const std::vector<SegmentSettings>& segs) {
  w.key("segments");
  w.beginArray();
  for (const auto& seg : segs) {
    w.beginObject();
    w.key("name");
    w.value(seg.name.c_str());
    w.key("px");
    w.value(static_cast<uint32_t>(seg.px));
    if (seg.pin) {
      w.key("pin");
      w.value(static_cast<uint32_t>(seg.pin));
    }
    w.key("colors");
    w.beginArray();
    for (const auto& c : seg.colors) w.value(colorToHexString(c).c_str());
    w.endArray();
    w.endObject();
  }
  w.endArray();
}

inline void writeLampJson(JsonWriter& w, const LampSettings& lamp) {
  w.key("lamp");
  w.beginObject();
  w.key("name");
  w.value(lamp.name.c_str());
  w.key("brightness");
  w.value(static_cast<uint32_t>(lamp.brightness));
  if (!lamp.password.empty()) {
    w.key("password");
    w.value(lamp.password.c_str());
  }
  w.key("setup");
  w.value(lamp.setup);
  w.key("named");
  w.value(lamp.named);
  w.key("advancedEnabled");
  w.value(lamp.advancedEnabled);
  w.key("webappEnabled");
  w.value(lamp.webappEnabled);
  w.key("apBootMinutes");
  w.value(static_cast<uint32_t>(lamp.apBootMinutes));
  w.key("brightnessCeiling");
  w.value(static_cast<uint32_t>(lamp.brightnessCeiling));
  w.key("socialMode");
  w.value(static_cast<uint32_t>(lamp.socialMode));
  w.endObject();
}

inline void writeLampOnlyJson(JsonWriter& w, const LampSettings& lamp) {
  w.beginObject();
  writeLampJson(w, lamp);
  w.endObject();
}

inline void writeStoredJson(JsonWriter& w, const LampSettings* lamp, const BaseSettings& base,
                     const ShadeSettings& shade, const HomeModeSettings& homeMode,
                     const ExpressionSettings* whole) {
  w.beginObject();
  if (lamp) writeLampJson(w, *lamp);
  w.key("base");
  w.beginObject();
  if (!base.byteOrder.empty()) {
    w.key("byteOrder");
    w.value(base.byteOrder.c_str());
  }
  w.key("colorsEditable");
  w.value(base.colorsEditable);
  writeSegmentsJson(w, base.segments);
  if (storesRotation(base)) writeRotateJson(w, base.rotation);
  if (whole) {
    w.key("knockout");
    w.beginArray();
    for (uint8_t level : base.knockoutPixels) w.value(static_cast<uint32_t>(level));
    w.endArray();
  }
  w.endObject();
  w.key("shade");
  w.beginObject();
  if (!shade.byteOrder.empty()) {
    w.key("byteOrder");
    w.value(shade.byteOrder.c_str());
  }
  w.key("colorsEditable");
  w.value(shade.colorsEditable);
  writeSegmentsJson(w, shade.segments);
  if (storesRotation(shade)) writeRotateJson(w, shade.rotation);
  w.endObject();
  w.key("homeMode");
  w.beginObject();
  w.key("ssid");
  w.value(homeMode.ssid.c_str());
  w.key("brightness");
  w.value(static_cast<uint32_t>(homeMode.brightness));
  w.key("enabled");
  w.value(homeMode.enabled);
  w.key("networkBound");
  w.value(homeMode.networkBound);
  w.key("socialDisabled");
  w.value(homeMode.socialDisabled);
  w.key("disabledExpressionTypes");
  w.beginArray();
  for (const auto& t : homeMode.disabledExpressionTypes) w.value(t.c_str());
  w.endArray();
  w.endObject();
  if (whole) {
    w.key("expressions");
    w.beginArray();
    for (const auto& expr : whole->expressions) config_codec::writeExpressionJson(w, expr);
    w.endArray();
  }
  w.endObject();
}

constexpr const char* kCountKey = "xn";
constexpr const char* kKnockoutKey = "ko";
// The rest (or a staged whole config) as a JSON blob, in two keys by turns so
// a rewrite never touches the live copy. With both present (a save cut short
// between writing one and erasing the other) the first wins: either is a
// whole copy, and saves and loads pick the same one.
constexpr const char* kRestKeys[2] = {"cfgb", "cfgc"};
constexpr const char* kLampCfgKey = "cfgl";
// Record slots every save sweeps ("x0".."x31"), past kMaxExpressions, so a
// record no count names is erased wherever it sits.
constexpr size_t kRecordKeySlots = 32;
inline void recordKey(size_t i, char (&key)[5]) {
  std::snprintf(key, sizeof(key), "x%u", static_cast<unsigned>(i));
}

inline void eraseSplitKeys(ConfigStore& store) {
  // Every record slot any build wrote, whatever the count says: a split save
  // cut short before its count landed leaves records the count never named.
  char key[5];
  for (size_t i = 0; i < kRecordKeySlots; ++i) {
    recordKey(i, key);
    store.erase(key);
  }
  store.erase(kCountKey);
  store.erase(kKnockoutKey);
}

inline void eraseRest(ConfigStore& store) {
  for (const char* key : kRestKeys) store.erase(key);
  store.erase(kLampCfgKey);
}

struct RestBlob {
  int slot = -1;  // the key holding the live blob, -1 for none
  // A rest key holds a blob that did not read: over the buffer, or a chunk
  // that failed its CRC (IDF erases the chunk then, and the blob on the next
  // read or boot).
  bool unreadable = false;
};

// The live rest blob, read into `buf`, NUL-terminated.
inline RestBlob liveRest(ConfigStore& store, char* buf, size_t cap) {
  RestBlob found;
  for (int i = 0; i < 2 && found.slot < 0; ++i) {
    const size_t len = store.readBlob(kRestKeys[i], buf, cap - 1);
    if (len > 0) {
      buf[len] = '\0';
      found.slot = i;
    } else if (store.has(kRestKeys[i])) {
      found.unreadable = true;
    }
  }
  return found;
}

// Writes the JSON `write` produces to the rest key that is not live, erased
// first, then erases the live one. A failed write touches only a key that
// held nothing live: IDF's cleanup of a failed blob write can take a chunk of
// the version it replaces.
template <class Write>
inline bool writeRest(ConfigStore& store, char* buf, size_t cap, Write write) {
  const RestBlob live = liveRest(store, buf, cap);
  JsonWriter w(buf, cap);
  write(w);
  w.finish();
  if (!w.ok()) return false;
  const char* target = kRestKeys[live.slot == 0 ? 1 : 0];
  if (!store.erase(target) || store.writeBlob(target, buf, w.length()) == 0) return false;
  return live.slot < 0 || store.erase(kRestKeys[live.slot]);
}

template <class Write>
inline size_t build(char* buf, size_t cap, Write write) {
  JsonWriter w(buf, cap);
  write(w);
  w.finish();
  return w.ok() ? w.length() : 0;
}

inline bool writeSplitKeys(ConfigStore& store, const BaseSettings& base,
                           const ExpressionSettings& expressions) {
  char key[5];
  const size_t count = std::min(expressions.expressions.size(), kMaxExpressions);
  uint8_t record[config_codec::kMaxExpressionRecordLen];
  for (size_t i = 0; i < count; ++i) {
    const size_t len = config_codec::encodeExpression(expressions.expressions[i], record);
    recordKey(i, key);
    if (store.writeBlob(key, record, len) == 0) return false;
  }
  if (!base.knockoutPixels.empty() &&
      store.writeBlob(kKnockoutKey, base.knockoutPixels.data(), base.knockoutPixels.size()) ==
          0) {
    return false;
  }
  // A string, which a failed rewrite leaves as it was.
  char countText[4];
  std::snprintf(countText, sizeof(countText), "%u", static_cast<unsigned>(count));
  if (store.write(kCountKey, countText) == 0) return false;
  // Every slot past the count, so a record left by a save cut short is gone.
  for (size_t i = count; i < kRecordKeySlots; ++i) {
    recordKey(i, key);
    store.erase(key);
  }
  return true;
}

inline SaveResult saveConfig(ConfigStore& store, const LampSettings& lamp,
                      const BaseSettings& base, const ShadeSettings& shade,
                      const ExpressionSettings& expressions,
                      const HomeModeSettings& homeMode, char* cfgBuffer,
                      size_t cfgBufferLen) {
  const auto whole = [&](JsonWriter& w) {
    writeStoredJson(w, &lamp, base, shade, homeMode, &expressions);
  };
  const auto rest = [&](JsonWriter& w) {
    writeStoredJson(w, nullptr, base, shade, homeMode, nullptr);
  };
  const auto lampOnly = [&](JsonWriter& w) { writeLampOnlyJson(w, lamp); };

  // Whole: everything in the one "cfg" string, the form every firmware reads.
  // A cfg string carrying an expressions array is the authority from the
  // moment it lands, so every other key goes after it.
  if (build(cfgBuffer, std::min(cfgBufferLen, kMaxWholeCfgLen + 1), whole) > 0) {
    if (store.write("cfg", cfgBuffer) == 0) {
      // NVS could not place the string beside the split keys. Stage it as a
      // rest blob, which may span pages and is the authority once the cfg
      // string is gone, then clear the room and place the string.
      if (!writeRest(store, cfgBuffer, cfgBufferLen, whole) || !store.erase("cfg")) {
        return SaveResult::WriteFailed;
      }
      // "cfgl" too: a save cut short can leave it, and the whole string is
      // placed with only the staged blob beside the system keys.
      eraseSplitKeys(store);
      store.erase(kLampCfgKey);
      if (build(cfgBuffer, cfgBufferLen, whole) == 0 || store.write("cfg", cfgBuffer) == 0) {
        return SaveResult::WriteFailed;
      }
    }
    eraseSplitKeys(store);
    eraseRest(store);
    return SaveResult::Ok;
  }

  // Split: the expressions as records beside the rest. The rest must fit the
  // blob form, checked before any write so TooLarge writes nothing.
  if (build(cfgBuffer, cfgBufferLen, rest) == 0) {
    return SaveResult::TooLarge;
  }

  // The rest as a blob and the lamp section alone in "cfg". The lamp section
  // lives in one of three places: a staged whole rest blob when that is the
  // live one, else "cfg" when it holds the lamp section alone, else "cfgl"
  // (a save cut short with "cfg" erased). Unless "cfg" holds it, the new lamp
  // section is staged in "cfgl" first, since from when "cfg" is erased until
  // the new one lands it is the only copy; a "cfgl" that is the current copy
  // stays as it is. A stale lamp-only "cfg" (a staged whole is live) goes
  // before the rest blob replaces that whole; any other "cfg" (a whole
  // string) is the authority until the rest blob lands, and goes after.
  const bool stagedWhole = liveRest(store, cfgBuffer, cfgBufferLen).slot >= 0 &&
                           std::strstr(cfgBuffer, "\"expressions\":") != nullptr;
  const bool cfgPresent = store.has("cfg");
  const size_t cfgLen = store.readString("cfg", cfgBuffer, cfgBufferLen);
  const bool cfgIsLampOnly = cfgLen > 0 && std::strstr(cfgBuffer, "\"base\":") == nullptr &&
                             std::strstr(cfgBuffer, "\"expressions\":") == nullptr;
  const bool cfgHoldsLamp = cfgIsLampOnly && !stagedWhole;
  const bool cfgIsAuthority = cfgPresent && !cfgIsLampOnly;
  const bool lampBlobHoldsLamp = !cfgPresent && !stagedWhole && store.has(kLampCfgKey);
  if (!cfgHoldsLamp && !lampBlobHoldsLamp) {
    const size_t len = build(cfgBuffer, cfgBufferLen, lampOnly);
    if (len == 0 || store.writeBlob(kLampCfgKey, cfgBuffer, len) == 0) {
      return SaveResult::WriteFailed;
    }
  }
  if ((cfgIsLampOnly && !cfgHoldsLamp && !store.erase("cfg")) ||
      !writeSplitKeys(store, base, expressions) ||
      !writeRest(store, cfgBuffer, cfgBufferLen, rest) ||
      (cfgIsAuthority && !store.erase("cfg"))) {
    return SaveResult::WriteFailed;
  }
  if (build(cfgBuffer, cfgBufferLen, lampOnly) == 0 || store.write("cfg", cfgBuffer) == 0) {
    return SaveResult::WriteFailed;
  }
  store.erase(kLampCfgKey);
  return SaveResult::Ok;
}

}  // namespace detail

using detail::saveConfig;
using detail::writeLampOnlyJson;
using detail::writeStoredJson;

}  // namespace legacy133
}  // namespace lamp
