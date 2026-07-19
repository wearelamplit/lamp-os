#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <string>
#include <utility>
#include <vector>

#include "config_store.hpp"
#include "config_types.hpp"
#include "disposition_store.hpp"

namespace lamp {

// Runtime lamp configuration; persisted to NVS under the "cfg" key.
class Config {
 private:
  ConfigStore* store_ = nullptr;

 public:
  // First-boot defaults a Lamp subclass injects before NVS load. Fields left
  // empty/default are not applied; the existing NVS value (or the Config
  // class-default) wins. colorsEditable is emitted in asBaseJson/asShadeJson
  // (the app hides the color picker when false).
  // Per-segment first-boot seed for a multi-segment role. Names the segment,
  // its pixel count, and a hex color list ("#RRGGBBWW,#…"). A variant supplies
  // one per physical strip; applyDefaults seeds the whole role's segment list
  // when NVS is still factory.
  struct SegmentDefault {
    std::string name;
    uint8_t px;
    std::string colors;
  };

  struct Defaults {
    std::string name;
    std::string baseColor;        // hex like "#300783"
    std::string shadeColor;
    bool baseColorsEditable  = true;
    bool shadeColorsEditable = true;
    // Per-surface pixel count for a first-boot lamp. 0 = no override
    // (leave whatever the loader / class default produced). When non-zero,
    // applyDefaults only overwrites NVS-loaded values that still match a
    // known factory baseline (same guard pattern as the color fields).
    uint8_t basePx  = 0;
    uint8_t shadePx = 0;
    // Force the lamp adopted at boot, skipping onboarding and the first-boot
    // random color roll. For fixed-install variants that ship curated colors.
    bool setup = false;
    // Multi-segment roles. Empty = single-segment scalar path (baseColor /
    // shadeColor + basePx / shadePx). Non-empty seeds every segment of the
    // role; the scalar fields are then unused for that role.
    std::vector<SegmentDefault> baseSegments;
    std::vector<SegmentDefault> shadeSegments;
  };

  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;

  Config() {};

  /**
   * create a config, loading the persisted blob from the store
   * @param [in] inStore persistence backend for NVS-backed values
   */
  Config(ConfigStore* inStore);

  // Attach a store to a default-constructed Config without running the
  // load constructor. Used by main.cpp to enable loadLampType() /
  // setLampType() during the variant-resolution chain that runs BEFORE
  // Lamp::setup() reconstructs Config via the store ctor.
  void setStore(ConfigStore* s) {
    store_ = s;
    dispositions_.attachStore(s);
  }

  // Apply first-boot defaults AFTER the NVS blob has been loaded. Only
  // fields that NVS left at their factory value are overwritten;
  // user-saved values are authoritative.
  void applyDefaults(const Defaults& d);

  /**
   * create a streamable json doc to send configs to the webserver
   * @return a JsonDocument to serialize
   */
  JsonDocument asJsonDocument();

  /**
   * Persist the current in-memory config to NVS under the "cfg" key.
   *
   * Used by live-preview drains (e.g. expressionOp) that want their change
   * to survive a reboot WITHOUT going through the settings_blob path's
   * fade-out-and-reboot. The runtime state has already been applied by
   * the caller; this just writes the canonical JSON to NVS.
   *
   * Returns true iff the store wrote > 0 bytes. On failure the in-memory
   * state is unchanged; the next call may succeed.
   *
   * `via`: short tag like "commit" / "settings_blob" / "expressionOp"
   * included in the success log to disambiguate which path triggered the write.
   * Pass a constant string literal.
   */
  bool persistConfig(const char* via);

  // Writes a caller-supplied whole-config JSON string straight to the NVS
  // "cfg" blob (the webapp's whole-document PUT path; the constructor
  // re-parses it on the next boot). Keeps the namespace/key contract here
  // rather than letting callers open Preferences("lamp") themselves.
  // Returns true iff the store wrote > 0 bytes.
  bool persistRawJson(const char* json);

  // Wipe persisted state (factory reset). Returns true on success. The caller
  // reboots afterward so defaults reload; in-memory state is left as-is.
  bool factoryReset();

  // Variant identity persistence. Called by main.cpp at
  // boot to resolve and persist the lampType. Stored in NVS under key
  // "lampType" in the existing "lamp" prefs namespace.
  void setLampType(const std::string& type);     // persists + updates in-memory
  std::string loadLampType();                     // reads NVS, returns empty if unset

  // This lamp's own mesh mac in canonical colon-hex, the same bytes peers
  // store for it as `mac` (the value it broadcasts as HELLO sourceMac). Seeded
  // once at mesh init; surfaced as `lampId` in the lamp section.
  void setLampId(const std::string& lampId);
  const std::string& lampId() const { return lampId_; }

  // Per-section serializers, each returning a String of just the JSON for
  // that section. Internal helpers backing the *JsonCached() accessors
  // below; the BLE onRead path always goes through the cache.
  String asLampJson();
  String asBaseJson();
  String asShadeJson();
  String asExpressionsJson();
  String asHomeModeJson();

  // Per-section JSON cache. Each section caches its serialised JSON plus a
  // dirty flag; the accessor rebuilds if dirty and copies the JSON into out,
  // both under a mutex in config.cpp, so it is safe from any task on either
  // core (never an ISR, the mutex blocks). Mutation paths must call
  // invalidateXSection() after touching any field that feeds asXJson().
  void lampSectionJsonCached(std::string& out);
  void baseSectionJsonCached(std::string& out);
  void shadeSectionJsonCached(std::string& out);
  void expressionsSectionJsonCached(std::string& out);
  void homeSectionJsonCached(std::string& out);

  // Rebuild a dirty section cache without the copy-out. For an off-BLE-path
  // caller (ble_control::tick) to warm the cache so the Core-0 read hits it
  // clean, without allocating a throwaway out-string every tick.
  void lampSectionRebuildIfDirty();
  void baseSectionRebuildIfDirty();
  void shadeSectionRebuildIfDirty();
  void expressionsSectionRebuildIfDirty();
  void homeSectionRebuildIfDirty();

  // Mark a section's cache dirty (a bool flip). Call after mutating any
  // field that contributes to the section's JSON shape.
  void invalidateLampSection();
  void invalidateBaseSection();
  // Whole-lamp current-draw anchors (mA) across every strip, emitted in
  // asBaseJson so the app can estimate battery runtime. idleMa is draw at
  // level 0, fullMa at level 255. No-op (no invalidate) when both are unchanged.
  void setDrawAnchors(uint16_t idleMa, uint16_t fullMa);
  void invalidateShadeSection();
  void invalidateExpressionsSection();
  void invalidateHomeSection();
  // Invalidate every section + the settings blob. Used by bulk-write paths
  // (settings_blob drain).
  void invalidateAllSections();

  // Per-peer social disposition (1=Salty, 2=Wary, 3=Neutral, 4=Fond,
  // 5=Smitten). Lives in a SEPARATE NVS key ("dispositions") from the
  // main config blob so the peer list can grow without bloating
  // CHAR_LAMP_SECTION / settings_blob. Stored as JSON object
  // { "AA:BB:CC:DD:EE:FF": 1..5 }; keys are lampId (mesh mac,
  // canonical-form colon-hex). Legacy name-keyed entries (older
  // firmware) are silently dropped on load via lamp::isValidBdAddr in
  // util/bd_addr.hpp. Bounded to ~100 entries. Per-lamp metadata,
  // never synced cross-mesh; each lamp has its own view.
  // Idle window before debounced disposition writes are committed to NVS.
  // 5s comfortably exceeds a worst-case slider-drag cadence (~20 Hz BLE
  // writes) while still feeling snappy if the user closes the app right
  // after touching the slider (the BLE disconnect path forces a flush).
  // See DispositionStore for the write-amplification guard.
  static constexpr uint32_t kDispositionFlushIdleMs = 5000;

  // Returns kDispositionDefault when the peer isn't in the store.
  // `lampId` is the mesh mac, canonical-form colon-hex (e.g.
  // "AA:BB:CC:DD:EE:FF"). See lamp::isValidBdAddr in util/bd_addr.hpp.
  uint8_t getDisposition(const std::string& lampId) const;
  // Clamps `value` to [1,5] and marks the debouncer dirty; the NVS write
  // happens later via maybeFlushDispositions() or flushDispositionsNow().
  // Evicts the lowest-by-key entry when at kDispositionsMax and the lampId
  // is new.
  void setDisposition(const std::string& lampId, uint8_t value);
  // Full JSON serialization for the CHAR_SOCIAL_DISPOSITIONS read path.
  String asDispositionsJson() const;
  // Bulk replace from the CHAR_SOCIAL_DISPOSITIONS write path. Caller
  // provides a JSON object; parse, clamp, mark dirty. The NVS commit is
  // deferred to the next maybeFlushDispositions/flushDispositionsNow call.
  // Returns true on success.
  bool setDispositionsFromJson(const char* json, size_t len);

  // Called from the loop drain on Core 1 every iteration. Cheap when no
  // disposition writes have happened (a dirty-flag check + a subtraction).
  // Triggers persistDispositions_() once the user has been idle for
  // kDispositionFlushIdleMs and clears the dirty flag.
  void maybeFlushDispositions(uint32_t nowMs);
  // Synchronous flush for when deferring is unsafe (BLE disconnect: phone is
  // gone, power may drop before the next loop tick). Must run on Core 1 (NVS
  // is not Core-0-safe). No-op when not dirty so onDisconnect can call it
  // unconditionally.
  void flushDispositionsNow();

 private:
  DispositionStore dispositions_{kDispositionFlushIdleMs};

  // All dirty=true initially so the first cached() call computes and
  // populates the string. After that, mutations on Core 1 must call
  // invalidateXSection() before reading the cached value again.
  std::string lampSectionJson_;
  std::string baseSectionJson_;
  std::string shadeSectionJson_;
  std::string expressionsSectionJson_;
  std::string homeSectionJson_;
  std::string lampId_;
  uint16_t drawIdleMa_ = 0;
  uint16_t drawFullMa_ = 0;
  bool lampSectionDirty_ = true;
  bool baseSectionDirty_ = true;
  bool shadeSectionDirty_ = true;
  bool expressionsSectionDirty_ = true;
  bool homeSectionDirty_ = true;
};
}  // namespace lamp
