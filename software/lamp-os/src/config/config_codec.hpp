#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "config_store.hpp"
#include "util/json_writer.hpp"
#include "config_types.hpp"

// JSON codec for the persisted "cfg" blob. Pure functions over the config
// model structs (no NVS, no Serial), so the parse/serialize logic runs in
// the native suite (round-trip tested in test/test_config_codec). Config owns
// the store, the section cache, and logging; the byte shape lives here.
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

// Serialize the model into root (the canonical "cfg" blob shape, also what
// the constructor re-parses on the next boot).
void toJson(JsonObject root, const LampSettings& lamp, const BaseSettings& base,
            const ShadeSettings& shade, const ExpressionSettings& expressions,
            const HomeModeSettings& homeMode);

// The same shapes written with no heap, byte-identical to serializeJson of
// expressionToJson / toStoredJson (pinned by test_json_writer).
void writeExpressionJson(JsonWriter& w, const ExpressionConfig& expr);
// `lamp` null leaves out the lamp section; `whole` non-null adds
// base.knockout and the expressions array, the shape of a whole save.
// writeLampOnlyJson writes {"lamp":{...}}, the split save's cfg string.
void writeLampOnlyJson(JsonWriter& w, const LampSettings& lamp);
void writeStoredJson(JsonWriter& w, const LampSettings* lamp, const BaseSettings& base,
                     const ShadeSettings& shade, const HomeModeSettings& homeMode,
                     const ExpressionSettings* whole);

// One expression's JSON object, the shape the BLE expressions section and the
// webapp document both carry: type, enabled, the interval pair, target, each
// parameter as its own key in key order, then colours.
void expressionToJson(JsonObject node, const ExpressionConfig& expr);

// Serialize everything but the expressions and the knockout levels (the "cfg"
// key's stored shape).
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

// Invalid and NoHeap come from saveRawJson, Held from Config while a stored
// web document waits for its reboot; each leaves the store untouched, as
// TooLarge does.
enum class SaveResult : uint8_t { Ok, WriteFailed, TooLarge, Invalid, NoHeap, Held };

// The save buffer's length: it holds a whole config up to kMaxWholeCfgLen
// and a split save's rest at the caps with every escapable string fully
// escaped (1746 bytes, measured by test_config_records).
constexpr size_t kMaxStoredCfgJsonLen = 1792;
// The largest whole config saved as the one "cfg" string. A string must fit
// in a single NVS page; at this size NVS can always place it with every
// other key at its cap (docs/dev/embedded-heap.md, pinned by
// test_config_records).
constexpr size_t kMaxWholeCfgLen = 1759;
static_assert(kMaxWholeCfgLen <= kMaxStoredCfgJsonLen, "the save buffer holds a whole config");

// Saves the model. A whole config up to kMaxWholeCfgLen is one "cfg"
// string carrying the expressions and knockout too, the form any firmware
// reads; when NVS cannot place it beside the split keys it is staged as a
// rest blob first, and the other keys are erased once it is authoritative.
// A larger config splits: each expression as a record ("x0", "x1", ...), the
// knockout levels in "ko", the count in "xn" (a decimal string), record slots
// past the count erased, the rest but the lamp section as a rest blob (JSON,
// in "cfgb" and "cfgc" by turns, the one not live erased and then written, so
// a rewrite never touches the live copy; with both present "cfgb" is live),
// and last the lamp section alone as the "cfg" string, staged in "cfgl" while
// "cfg" is replaced. The JSON is checked
// before any write, so TooLarge leaves the store untouched; an unchanged key
// is not rewritten (NVS skips identical writes). A save takes no
// config-sized heap: the JSON goes into `cfgBuffer` and records come off the
// stack, and only NVS itself allocates, a handle per call and small bookkeeping
// per page. Stops at the first failed write; the caller keeps its dirty state
// and retries.
SaveResult saveConfig(ConfigStore& store, const LampSettings& lamp,
                      const BaseSettings& base, const ShadeSettings& shade,
                      const ExpressionSettings& expressions,
                      const HomeModeSettings& homeMode, char* cfgBuffer,
                      size_t cfgBufferLen);

struct LoadedRecords {
  bool present = false;      // the expressions came from records
  uint32_t droppedMask = 0;  // bit i: record "x<i>" missing or unreadable
  uint8_t duplicates = 0;    // records skipped as a repeat (type, target)
  // A stored rest or lamp blob did not read, or a split store has no rest:
  // Config then serves defaults without saving them over the store.
  bool unreadable = false;
};

// Overlays what a split save keeps outside the stored JSON. The JSON wins
// wherever it carries the data itself: knockout levels from "ko" only when it
// has no base.knockout, expressions from their records only when it has no
// expressions array (an older firmware always writes both, so a config it
// saved after a downgrade is what loads). A record that fails to read drops
// only itself (droppedMask); a repeated (type, target) keeps its first record.
// Call after fromJson, which sizes the knockout.
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
// largest single block is a pool of slots, at most kRawPoolBytes, or the
// longest string's growth, at most twice it plus kRawStringSlack.
constexpr size_t kRawSlotBytes = 15;
constexpr size_t kRawStringBytes = 24;
constexpr size_t kRawLongestPerByte = 4;
constexpr size_t kRawModelBytes = 4096;
constexpr size_t kRawPoolBytes = 4096;
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
SaveResult saveRawJson(ConfigStore& store, const char* json, char* cfgBuffer,
                       size_t cfgBufferLen, const std::string& keptPassword);

}  // namespace config_codec
}  // namespace lamp
