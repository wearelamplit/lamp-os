#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <string>
#include <utility>
#include <vector>

#include "config_types.hpp"

namespace lamp {

// Clock-only debounce helper for disposition NVS writes: a dirty flag plus
// the timestamp of the most recent mutation. No NVS access, no I/O.
// The test mirror in test/test_disposition_debounce reimplements this shape;
// keep it in sync when the API changes.
class DispositionDebouncer {
 public:
  explicit DispositionDebouncer(uint32_t idleMs) : idleMs_(idleMs) {}

  void markDirty(uint32_t nowMs) {
    dirty_ = true;
    lastMarkMs_ = nowMs;
  }

  bool dirty() const { return dirty_; }

  // Returns true iff dirty AND the idle window has elapsed since the
  // most recent markDirty. Caller is responsible for the actual flush
  // and then calling clear(). Subtraction-based comparison so millis()
  // wraparound (every ~49 days) doesn't strand the dirty flag forever.
  bool shouldFlush(uint32_t nowMs) const {
    if (!dirty_) return false;
    return (nowMs - lastMarkMs_) >= idleMs_;
  }

  void clear() {
    dirty_ = false;
    lastMarkMs_ = 0;
  }

 private:
  bool dirty_ = false;
  uint32_t lastMarkMs_ = 0;
  uint32_t idleMs_;
};

/**
 * @brief configurations file for the lamp that can be modified on the web
 * portal
 * @property lamp - global lamp details
 * @property base - details about the neopixels in the lamp base
 * @property shade - details about the neopixels in the lamp bulb
 */
class Config {
 private:
  Preferences* prefs;

 public:
  // First-boot defaults a Lamp subclass injects before NVS load. Fields left
  // empty/default are not applied; the existing NVS value (or the Config
  // class-default) wins. colorsEditable is emitted in asBaseJson/asShadeJson
  // (the app hides the color picker when false).
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
  };

  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;

  Config() {};

  /**
   * @brief create a config based on information in the user's storage
   * @param [in] inPrefs preferences container for nvs values
   */
  Config(Preferences* inPrefs);

  // Attach a Preferences* to a default-constructed Config without
  // running the NVS-load constructor. Used by main.cpp to enable
  // loadLampType() / setLampType() during the variant-resolution chain
  // that runs BEFORE Lamp::setup() reconstructs Config via the
  // Preferences* ctor.
  void setPrefs(Preferences* p) { prefs = p; }

  // Apply first-boot defaults AFTER the NVS blob has been loaded. Only
  // fields that NVS left at their factory value are overwritten;
  // user-saved values are authoritative.
  void applyDefaults(const Defaults& d);

  /**
   * @brief create a streamable json doc to send configs to the webserver
   * @return a JsonDocument to serialize
   */
  JsonDocument asJsonDocument();

  /**
   * @brief Persist the current in-memory config to NVS under the "cfg" key.
   *
   * Used by live-preview drains (e.g. expressionOp) that want their change
   * to survive a reboot WITHOUT going through the settings_blob path's
   * fade-out-and-reboot. The runtime state has already been applied by
   * the caller; this just writes the canonical JSON to NVS.
   *
   * Returns true iff prefs.begin() succeeded and putString wrote > 0
   * bytes. On failure the in-memory state is unchanged; the next call
   * may succeed.
   */
  // `via` is a short tag like "commit" / "settings_blob" / "expressionOp"
  // included in the success log so fleet debugging can disambiguate
  // which path triggered the write. Pass a constant string literal.
  bool persistConfig(const char* via);

  // Writes a caller-supplied whole-config JSON string straight to the NVS
  // "cfg" blob (the webapp's whole-document PUT path; the constructor
  // re-parses it on the next boot). Keeps the namespace/key contract here
  // rather than letting callers open Preferences("lamp") themselves.
  // Returns true iff putString wrote > 0 bytes.
  bool persistRawJson(const char* json);

  // Variant identity persistence. Called by main.cpp at
  // boot to resolve and persist the lampType. Stored in NVS under key
  // "lampType" in the existing "lamp" prefs namespace.
  void setLampType(const std::string& type);     // persists + updates in-memory
  std::string loadLampType();                     // reads NVS, returns empty if unset

  // Per-section serializers, each returning a String of just the JSON for
  // that section. Internal helpers backing the *JsonCached() accessors
  // below; the BLE onRead path always goes through the cache.
  String asLampJson();
  String asBaseJson();
  String asShadeJson();
  String asExpressionsJson();
  String asHomeModeJson();

  // Per-section JSON cache. Each section caches its serialised JSON plus a
  // dirty flag; the CHAR_*_SECTION read on Core 0 hands back the cached
  // string and NimBLE copies it, so reads never re-touch Config. Mutation
  // paths on Core 1 must call invalidateXSection() after touching any field
  // that feeds asXJson(); the cache rebuilds lazily on next read. Safe from
  // either core (a portMUX in config.cpp serialises the rebuild).
  const std::string& lampSectionJsonCached();
  const std::string& baseSectionJsonCached();
  const std::string& shadeSectionJsonCached();
  const std::string& expressionsSectionJsonCached();
  const std::string& homeSectionJsonCached();

  // Mark a section's cache dirty (a bool flip). Call after mutating any
  // field that contributes to the section's JSON shape.
  void invalidateLampSection();
  void invalidateBaseSection();
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
  // { "AA:BB:CC:DD:EE:FF": 1..5 }; keys are canonical-form BD_ADDR
  // strings (non-BD_ADDR keys are dropped on load via lamp::isValidBdAddr
  // in util/bd_addr.hpp). Bounded to ~100 entries. Per-lamp metadata, never
  // synced cross-mesh; each lamp has its own view.
  static constexpr uint8_t kDispositionDefault = 3;
  static constexpr size_t kDispositionsMax = 100;
  // Idle window before debounced disposition writes commit to NVS. 5s
  // exceeds a worst-case slider-drag cadence (~20 Hz BLE writes) while still
  // feeling snappy if the user closes the app right after the slider (the
  // BLE disconnect path forces a flush).
  static constexpr uint32_t kDispositionFlushIdleMs = 5000;

  // Returns kDispositionDefault when the peer isn't in the store.
  // `bdAddr` is canonical-form colon-hex (e.g. "AA:BB:CC:DD:EE:FF").
  // See lamp::isValidBdAddr in util/bd_addr.hpp.
  uint8_t getDisposition(const std::string& bdAddr) const;
  // Clamps `value` to [1,5] and marks the debouncer dirty; the NVS write
  // happens later via maybeFlushDispositions() or flushDispositionsNow().
  // Evicts the lowest-by-key entry when at kDispositionsMax and the BD_ADDR
  // is new.
  void setDisposition(const std::string& bdAddr, uint8_t value);
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
  // Sorted vector; lookups use std::lower_bound. Invariant: entries are
  // ALWAYS sorted by key (lexicographic); every mutation site must preserve
  // this so getDisposition's binary search stays correct.
  std::vector<std::pair<std::string, uint8_t>> dispositions_;
  DispositionDebouncer dispositionsDebouncer_{kDispositionFlushIdleMs};
  void loadDispositionsFromPrefs_();
  // Returns true when the NVS write succeeded; false if prefs.begin() failed
  // (NVS full / partition corrupt). Callers should leave the dirty flag set
  // on failure so the next flush attempt retries.
  bool persistDispositions_();

  // All dirty=true initially so the first cached() call computes and
  // populates the string. After that, mutations on Core 1 must call
  // invalidateXSection() before reading the cached value again.
  std::string lampSectionJson_;
  std::string baseSectionJson_;
  std::string shadeSectionJson_;
  std::string expressionsSectionJson_;
  std::string homeSectionJson_;
  bool lampSectionDirty_ = true;
  bool baseSectionDirty_ = true;
  bool shadeSectionDirty_ = true;
  bool expressionsSectionDirty_ = true;
  bool homeSectionDirty_ = true;
};
}  // namespace lamp
