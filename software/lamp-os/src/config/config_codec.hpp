#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "config_store.hpp"
#include "util/json_writer.hpp"
#include "util/str.hpp"
#include "config_types.hpp"

// Codec for the stored config: the settings blob and expression records it
// saves, the JSON forms releases up to 1.3.3 stored (loaded, never written),
// and the JSON the web document and BLE sections carry. Pure functions over
// the config model structs and a ConfigStore (no Serial), so it runs in the
// native suite. Config owns the store, the section cache, and logging.
namespace lamp {
namespace config_codec {

// The stored parameters for (type, target), or null when the list has no
// such entry.
const ExpressionParams* findExpressionParams(const ExpressionSettings& settings,
                                             const std::string& type, uint8_t target);

// Mirrors an upsert into the stored list: replaces the (type, target) entry,
// or appends while under kMaxExpressions, growing the vector four slots at a
// time. False when the list is full and the entry was not stored.
bool mirrorUpsert(ExpressionSettings& settings, const ExpressionConfig& cfg);

// `s` when it names a byte order the strip driver knows ("GRBW", "GRB",
// "BGR"), else "" (the variant's default). Bounds the stored field.
std::string storableByteOrder(const char* s);

// Parse one expression entry (a stored "expressions" element or an upsert
// op's "entry"), dropping colours and parameters past the caps. The target is
// copied as given. Returns false when a cap dropped something.
bool parseExpression(JsonObject node, ExpressionConfig& out, bool enabledDefault);

// Applies each key of a `rotate` object that has the right type and value:
// `on` a boolean, `periodS` an integer read as 32-bit signed and clamped to
// [kRotationMinPeriodS, kRotationMaxPeriodS], `dir` "up" or "down". Any other
// key, type or value leaves that field as it was.
void parseRotation(JsonObjectConst node, ColorRotation& out);
// Writes a role's `segments` array into its page section. Every segment
// carries `pin` (0 for the variant pin) and `builtinPin` (its strip's variant
// pin, 0 with no strip), and the app shows the pin control only to firmware
// that sends them.
void writeSectionSegments(JsonObject section, const std::vector<SegmentSettings>& segs);
// Adds `rotate`, every key present, to the role's page section when the role
// offers rotation, and nothing otherwise.
void writeSectionRotate(JsonObject section, const ShadeSettings& shade);
void writeSectionRotate(JsonObject section, const BaseSettings& base);
// Parse the "cfg" blob into the model. Applies the field defaults, clamps,
// byte-order derivation, and legacy migrations the loader relies on. Leaves
// each struct at its class default for any absent key. Returns false when an
// expression cap truncated the stored list; the caller logs it, and the next
// save writes the truncated form.
bool fromJson(JsonObject root, LampSettings& lamp, BaseSettings& base,
              ShadeSettings& shade, ExpressionSettings& expressions,
              HomeModeSettings& homeMode);

// Serialize the model into root: the web GET document, in the shape of the
// whole "cfg" string releases up to 1.3.3 stored.
void toJson(JsonObject root, const LampSettings& lamp, const BaseSettings& base,
            const ShadeSettings& shade, const ExpressionSettings& expressions,
            const HomeModeSettings& homeMode);

// expressionToJson's shape written with no heap, byte-identical to
// serializeJson of it (pinned by test_json_writer).
void writeExpressionJson(JsonWriter& w, const ExpressionConfig& expr);

// One expression's JSON object, the shape the BLE expressions section and the
// webapp document both carry: type, enabled, the interval pair, target, each
// parameter as its own key in key order, then colours.
void expressionToJson(JsonObject node, const ExpressionConfig& expr);

// Serialize everything but the expressions and the knockout levels.
void toStoredJson(JsonObject root, const LampSettings& lamp,
                  const BaseSettings& base, const ShadeSettings& shade,
                  const HomeModeSettings& homeMode);

// One expression as a compact record: a format byte, flags, target, the type
// id, the interval pair, RGBW colours, then (param key index, value) pairs.
// Longest at the caps is kMaxExpressionRecordLen. Parameters whose key has no
// index are skipped.
constexpr size_t kMaxExpressionRecordLen =
    4 + kMaxExpressionTypeLen + 8 + 1 + 4 * kMaxExpressionColors + 1 +
    5 * kMaxExpressionParams;
size_t encodeExpression(const ExpressionConfig& expr, uint8_t* out);
bool decodeExpression(const uint8_t* data, size_t len, ExpressionConfig& out);

// Everything stored but the expressions, as one compact blob: a format byte,
// the lamp section, base and shade (segments, pins, byte order, rotation), the
// base knockout levels, then home mode. Decoding loads the model fromJson
// loads from the same model's stored JSON, field for field, so every JSON
// shape built from the model is unchanged (pinned by test_config_settings_codec).
// colorsEditable is not stored, since applyDefaults sets it from the variant.
// Longest at the caps is kMaxSettingsLen; `out` must hold kMaxSettingsBlobLen.
constexpr size_t kMaxKnockoutLevels = 255;  // one per pixel, and a role holds 255
constexpr size_t kMaxSettingsLen =
    1 + (5 + 1 + kMaxLampNameLen + 1 + kMaxPasswordLen) +
    2 * (4 + kMaxSegments * (1 + kMaxSegmentNameLen + 3 + 4 * kMaxSurfaceColors)) +
    (1 + kMaxKnockoutLevels) +
    (2 + 1 + kMaxSsidLen + 1 + kMaxDisabledExpressionTypes * (1 + kMaxExpressionTypeLen));
constexpr size_t kMaxSettingsBlobLen = 1024;
static_assert(kMaxSettingsLen <= kMaxSettingsBlobLen, "the settings blob holds the caps");
size_t encodeSettings(const LampSettings& lamp, const BaseSettings& base,
                      const ShadeSettings& shade, const HomeModeSettings& homeMode,
                      uint8_t* out);
// True when `data` is exactly what encodeSettings writes for the model.
bool settingsMatch(const uint8_t* data, size_t len, const LampSettings& lamp,
                   const BaseSettings& base, const ShadeSettings& shade,
                   const HomeModeSettings& homeMode);
// False, leaving the model untouched, for a truncated blob, an unknown format
// byte, trailing bytes, a count or length over its cap, or an unknown flag or
// code. Fields a role leaves out keep the value passed in, as fromJson's do.
bool decodeSettings(const uint8_t* data, size_t len, LampSettings& lamp, BaseSettings& base,
                    ShadeSettings& shade, HomeModeSettings& homeMode);

// Invalid and NoHeap come from saveRawJson, Held from Config while a stored
// web document waits for its reboot; each leaves the store untouched, as
// TooLarge does.
enum class SaveResult : uint8_t { Ok, WriteFailed, TooLarge, Invalid, NoHeap, Held };

// The longest rest or staged whole blob a release up to 1.3.3 wrote, which
// the legacy load reads (1818 bytes fully escaped, plus slack).
constexpr size_t kMaxStoredCfgJsonLen = 1824;

// Saves the model as expression records and the settings blob, moving a
// store a release up to 1.3.3 wrote to that form. A reset at any write leaves
// each section loading as before or as saved, and the legacy keys go only
// once the new form has read back equal (docs/dev/embedded-heap.md, "The NVS
// budget"). `buffer` must hold kMaxSettingsBlobLen bytes, else TooLarge with
// nothing written. Takes no heap beyond NVS's own. A failed write or
// mismatched read-back returns WriteFailed; the caller keeps its dirty state
// and retries.
SaveResult saveConfig(ConfigStore& store, const LampSettings& lamp,
                      const BaseSettings& base, const ShadeSettings& shade,
                      const ExpressionSettings& expressions,
                      const HomeModeSettings& homeMode, uint8_t* buffer, size_t bufferLen);

struct LoadedRecords {
  bool present = false;      // the expressions came from records
  uint32_t droppedMask = 0;  // bit i: record "x<i>" missing or unreadable
  uint8_t duplicates = 0;    // records skipped as a repeat (type, target)
  // A stored settings, rest or lamp blob did not read or decode, or a split
  // store has no rest. Config then holds the first-boot save, so what loaded
  // in their place is not saved over them.
  bool unreadable = false;
};

// Overlays the records and the legacy knockout blob ("ko") on a loaded
// model. The binary load passes cfgHasKnockout, its blob holding the levels.
// For a legacy store the JSON wins wherever it carries the data itself:
// knockout levels from "ko" only when it has no base.knockout, expressions
// from their records only when it has no expressions array (an older
// firmware always writes both, so a config it saved after a downgrade is
// what loads). A record that fails to read drops
// only itself (droppedMask); a repeated (type, target) keeps its first record.
// Call after fromJson or decodeSettings, which size the knockout.
LoadedRecords loadRecords(ConfigStore& store, BaseSettings& base,
                          ExpressionSettings& expressions, bool cfgHasKnockout,
                          bool cfgHasExpressions);

// Loads a stored config from the parsed "cfg" string (`root`): whole when it
// carries an expressions array (the authority over every other key; any rest
// or lamp blob beside it is erased), else from the live rest blob, whole when
// it carries one and otherwise the rest beside the records, the knockout blob
// and the lamp section from the cfg string or "cfgl". `withinCaps` reports
// fromJson's result.
LoadedRecords loadStoredConfig(ConfigStore& store, JsonObject root, LampSettings& lamp,
                               BaseSettings& base, ShadeSettings& shade,
                               ExpressionSettings& expressions,
                               HomeModeSettings& homeMode, bool& withinCaps);

// The form the stored config loaded from. Whole and Split are the forms
// releases up to 1.3.3 wrote (a whole "cfg" string, or "cfg", "cfgb"/"cfgc",
// "cfgl" and "ko" beside the records); Binary is the settings blob.
enum class StoreForm : uint8_t { Empty, Binary, Whole, Split };
const char* storeFormName(StoreForm form);
// A form the first save moves to the settings blob.
inline bool isLegacyForm(StoreForm form) {
  return form == StoreForm::Whole || form == StoreForm::Split;
}

struct LoadedConfig {
  StoreForm form = StoreForm::Empty;
  LoadedRecords records;
  bool withinCaps = true;
  // The lamp section carries `named`, so applyDefaults need not infer it.
  bool namedKeyPresent = false;
  // A "cfg" string holding data that did not parse; the model is left at
  // class defaults and nothing else loads.
  bool parseFailed = false;
  size_t cfgBytes = 0;  // the "cfg" string's length, 0 for a binary store
};

// Loads the stored config. A settings blob that reads is the authority: it
// loads with the records, and every legacy key beside it is erased. With
// none, the legacy keys load through loadStoredConfig, as releases up to
// 1.3.3 wrote them. Both blobs
// present (a save cut short) loads "st0". A settings blob that is present
// but does not read or decode is reported unreadable, and the legacy load
// runs for whatever is left. The blob is read into `buffer`, which holds
// kMaxSettingsBlobLen bytes.
LoadedConfig loadConfig(ConfigStore& store, LampSettings& lamp, BaseSettings& base,
                        ShadeSettings& shade, ExpressionSettings& expressions,
                        HomeModeSettings& homeMode, uint8_t* buffer, size_t bufferLen);

// Heap a raw save takes beside its document, bounded from the host
// measurements in test_config_persist_validate (64-bit, so its slots and
// pools are twice the ESP32's) with margin, over the documents that test
// builds, up to the web PUT's 16 KB. In all, the parse and the model take at
// most kRawSlotBytes per structural token (`{`, `[`, `:`, `,`), a byte per
// document byte, kRawStringBytes per distinct string (ArduinoJson stores each
// distinct key or value once), kRawLongestPerByte per byte of the longest
// string (grown by doubling while it is parsed, so up to twice it, then kept
// and copied into the model) and
// kRawModelBytes for the model at the caps, which carries some slack. The
// largest single block is a pool of slots (at most kRawPoolBytes), the
// model's expression list at its cap (kRawExpressionListBytes) or the longest
// string's growth (at most twice it plus kRawStringSlack).
constexpr size_t kRawSlotBytes = 15;
constexpr size_t kRawStringBytes = 24;
constexpr size_t kRawLongestPerByte = 4;
constexpr size_t kRawModelBytes = 4096;
constexpr size_t kRawPoolBytes = 4096;
constexpr size_t kRawExpressionListBytes = kMaxExpressions * sizeof(ExpressionConfig);
constexpr size_t kRawStringSlack = 128;

// What a raw save's heap gate reads off a document without parsing it.
struct RawJsonShape {
  size_t len = 0;
  size_t tokens = 0;    // structural characters, counted inside strings too
  size_t longest = 0;   // the longest string's raw length
  size_t distinct = 0;  // strings, keys included, not seen just before
  // Strict JSON: no single-quoted string, no unquoted key or value, which
  // ArduinoJson would parse into strings the counts above miss.
  bool strict = true;
  size_t heapBytes() const;
  size_t largestBlock() const;
};
RawJsonShape scanRawJson(const char* json);
// A parse's error as a save's: Ok, NoHeap for want of memory, else Invalid.
SaveResult rawParseResult(DeserializationError error);

// Saves a whole config document (the webapp's whole-document PUT) through
// fromJson and saveConfig, for the next boot to load, parsing it once.
// Invalid when it is not strict JSON, does not parse, carries no `lamp`
// object (so a truncated body cannot replace a good config), or carries a
// password or SSID over its cap; NoHeap when the free heap cannot hold the
// parse and the model, or the largest free block their largest single
// block, so no allocation on the way can throw, and when the parse runs out
// of memory all the same. A document with no password keeps `keptPassword`,
// the GET document having redacted it, and one naming the lamp marks it
// named.
SaveResult saveRawJson(ConfigStore& store, const char* json, uint8_t* buffer, size_t bufferLen,
                       const std::string& keptPassword);

}  // namespace config_codec
}  // namespace lamp
