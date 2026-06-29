# Wisp Revive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the wisp to a working, mesh-visible state and close the concurrency / input-hardening defects an expert audit confirmed in the dev wisp firmware.

**Architecture:** Six independent fixes in `software/wisp/`, ordered by severity. Task 1 is the show-stopper (the wisp emits no presence beacon — invisible to the fleet). Tasks 2–4 fix a shared root cause: `portMUX` (a spinlock) guarding heap-allocating, cross-task code. Tasks 5–6 harden untrusted input (Aurora WebSocket, app-pushed zone ids) and a `millis()`-wrap eviction bug. Every functional fix lands with a native regression test, because the audit's headline bug existed precisely because the native suite never exercised the HELLO emit path.

**Tech Stack:** C++17, PlatformIO, ESP-IDF/Arduino-ESP32 (Seeed Xiao ESP32-C6), FreeRTOS, ArduinoJson v7, nanopb, miniz, Unity (native test).

## Global Constraints

- **Mesh wire format + version range are a lock-in.** `PROTOCOL_VERSION_EMIT = 0x05`, `RX_MIN = 0x04`, `RX_MAX = 0x05`. None of these fixes touch the wire contract, `CONTROL_MAX_PAYLOAD = 230`, the dedup ring (64), or `kGattSchemaVersion`. If a change would, STOP — it's out of scope.
- **Run everything through `npm run …`**, never raw `pio`/`adb`. Build: `npm run wisp:build`. Test: `npm run wisp:test`. Both run from repo root.
- **Baseline is green:** `npm run wisp:test` = 40/40 before you start. Keep it green; the suite grows with each task.
- **Code conventions (CLAUDE.md):** default to NO comment; no phase/ticket/date-archaeology vocabulary; no speculative API surface; framework code takes no variant headers. When you rewrite a threading comment, state the real invariant, not the history.
- **Worktree:** all work happens in `.claude/worktrees/wisp-revive` on branch `wisp-revive` (branched from `dev`). Do not push until the user confirms readiness.
- **`software/wisp/src/lamp_protocol.hpp` is a mirror** of the lamp's authoritative copy — do not edit it in this plan (no task needs to).
- **StatusBeacon.cpp / CurrentPalette.cpp / WispConfig.cpp are excluded from the native build** (`build_src_filter` in `software/wisp/platformio.ini` — they pull `<Arduino.h>`/`<WiFi.h>`/FreeRTOS). Tests therefore target header-only builders (`lamp_protocol::build*`), native-buildable classes (`WispZoneSelector`, `WispRoster`), or newly-extracted free functions — never instantiate those three classes in a test.

---

### Task 1: Fix the WISP_HELLO emit buffer (wisp is invisible on the mesh)

**Severity: CRITICAL.** `emit()` sizes its buffer to `WISP_HELLO_FIXED_SIZE` (45), but `buildWispHello` writes the v0x05 `tlv_count` trailer byte at index 45 and needs ≥46 bytes, so it returns 0. `emit()` then does `if (!n) return;` *above* both the `MSG_WISP_HELLO` broadcast and the `MSG_WISP_CLAIM` broadcast — so every 2 s tick emits **nothing**. The wisp is never "paired" (no HELLO in 60 s → can't push brightness < 5), `CHAR_WISP_STATUS`'s HELLO half goes stale, and multi-wisp claims never propagate.

**Files:**
- Create: `software/wisp/test/test_wisp_hello_build/wisp_hello_build.cpp`
- Modify: `software/wisp/src/StatusBeacon.cpp:178`

**Interfaces:**
- Consumes: `lamp_protocol::buildWispHello(uint8_t* buf, size_t bufLen, uint16_t seq, const uint8_t* sourceMac, uint32_t version, uint8_t flags, const char* palettePrefix, size_t palettePrefixLen, const char* fwChannel, size_t fwChannelLen, uint32_t fwVersion)` → returns bytes written (46) or 0; `lamp_protocol::parseWispHello(const uint8_t*, size_t, ParsedWispHello&)` → bool; constants `WISP_HELLO_FIXED_SIZE` (45), `WISP_HELLO_MAX_SIZE` (96).
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Write the failing test**

Create `software/wisp/test/test_wisp_hello_build/wisp_hello_build.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "lamp_protocol.hpp"

using namespace lamp_protocol;

static const uint8_t kMac[6] = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};

void setUp() {}
void tearDown() {}

// The exact trap that bit StatusBeacon: a FIXED_SIZE buffer is too small for
// the v0x05 tlv_count trailer, so the builder refuses and returns 0.
void test_fixed_size_buffer_is_insufficient() {
  uint8_t tooSmall[WISP_HELLO_FIXED_SIZE];
  size_t n = buildWispHello(tooSmall, sizeof(tooSmall), 1, kMac,
                            0x00010000u, 0x00, "abcd", 4, nullptr, 0, 0);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

// A correctly-sized buffer yields a 46-byte frame with tlv_count = 0 that
// round-trips through the parser.
void test_max_size_buffer_builds_and_parses() {
  uint8_t buf[WISP_HELLO_MAX_SIZE];
  size_t n = buildWispHello(buf, sizeof(buf), 7, kMac,
                            0x00010000u, 0x02, "abcd", 4, nullptr, 0, 0);
  TEST_ASSERT_EQUAL_UINT(WISP_HELLO_FIXED_SIZE + 1, n);   // 46
  TEST_ASSERT_EQUAL_UINT8(0, buf[WISP_HELLO_FIXED_SIZE]); // tlv_count

  ParsedWispHello out;
  TEST_ASSERT_TRUE(parseWispHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0x02, out.flags);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_size_buffer_is_insufficient);
  RUN_TEST(test_max_size_buffer_builds_and_parses);
  return UNITY_END();
}
```

> Note: confirm the exact `buildWispHello` / `parseWispHello` signatures and `ParsedWispHello` field name (`flags`) against `software/wisp/src/lamp_protocol.hpp` before running — match them verbatim. The test directory is auto-discovered by PlatformIO (same pattern as `test_artnet_frame`); no env wiring needed.

- [ ] **Step 2: Run the test to verify it builds and the assertions describe current behavior**

Run: `npm run wisp:test`
Expected: the new `test_wisp_hello_build` env appears and **passes** — `buildWispHello` already returns 0 for a 45-byte buffer (that IS the bug) and 46 for a 96-byte buffer. This test pins the builder contract; it is the guard that the *next* step's StatusBeacon buffer must satisfy. If it fails to compile, fix the signature/field names to match the header.

- [ ] **Step 3: Fix the StatusBeacon buffer**

In `software/wisp/src/StatusBeacon.cpp:178`, change:

```cpp
  uint8_t buf[lamp_protocol::WISP_HELLO_FIXED_SIZE];
```
to:
```cpp
  uint8_t buf[lamp_protocol::WISP_HELLO_MAX_SIZE];
```

Rationale for `MAX` over the exact `FIXED_SIZE + 1`: every other builder buffer in this file (`claimBuf[WISP_CLAIM_MAX_SIZE]`, `frame[CONTROL_MAX_SIZE]`, `frame[WISP_PALETTE_MAX_SIZE]`) sizes to the builder's `*_MAX_SIZE`. The HELLO path was the lone `*_FIXED_SIZE` outlier — that inconsistency is the bug. `MAX` costs ~51 bytes of timer-task stack and survives the first day a HELLO TLV is added.

- [ ] **Step 4: Verify the firmware compiles**

Run: `npm run wisp:build`
Expected: SUCCESS (clean build, no new warnings on `StatusBeacon.cpp`).

- [ ] **Step 5: Run the full native suite**

Run: `npm run wisp:test`
Expected: PASS — 40 prior + 2 new = 42 test cases, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add software/wisp/src/StatusBeacon.cpp software/wisp/test/test_wisp_hello_build
git commit -m "fix(wisp): size WISP_HELLO emit buffer for the v0x05 tlv trailer

emit() allocated WISP_HELLO_FIXED_SIZE (45) but buildWispHello writes the
tlv_count trailer at index 45 and needs 46, so it returned 0 and emit()
early-returned above both the HELLO and CLAIM broadcasts — the wisp emitted
no presence beacon at all. Size to WISP_HELLO_MAX_SIZE like every other
builder buffer in the file; add a native test pinning the builder contract.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Snapshot the manual palette across the task boundary (use-after-realloc)

**Severity: HIGH.** `StatusBeacon::emitPalette()` runs on the timer-service task and iterates `config_->manualPalette()` (a `std::vector`) with **no lock**, while `WispConfig::setManualPalette()` runs on the loop task and `.assign()`s — reallocating — that same vector. Real cross-task use-after-realloc → crash or garbage colors. `emitMux_` guards a different object and can't help. Fix mirrors the proven `CurrentPalette::copyPaletteIdPrefix` snapshot + `WispRoster` mutex pattern.

**Files:**
- Modify: `software/wisp/src/WispConfig.h` (add ctor/dtor, mutex member, `copyManualPalette`)
- Modify: `software/wisp/src/WispConfig.cpp` (create/delete mutex; guard `setManualPalette`; implement `copyManualPalette`)
- Modify: `software/wisp/src/StatusBeacon.cpp` (`emitPalette` → use the snapshot; rewrite the stale threading comment)

**Interfaces:**
- Consumes: existing `WispConfig::manualPalette()` (the racing `std::vector<ManualPaletteColor>`), `WispConfig::setManualPalette(...)`; the `WispRoster` mutex idiom (`xSemaphoreCreateMutex`, `asHandle`, `xSemaphoreTake/Give`) — copy it verbatim from `WispRoster.cpp`.
- Produces: `size_t WispConfig::copyManualPalette(uint8_t* outRgb, size_t maxColors) const` — packs up to `maxColors` RGB triples into `outRgb` (needs `maxColors*3` bytes), returns the color count written, lock-guarded; the only `manualPalette` accessor safe to call off the loop task.

> No native test: `WispConfig` and `StatusBeacon` are both excluded from the native build (Arduino `Preferences`/`String`, FreeRTOS). This task is verified by `npm run wisp:build` + the loop's own correctness review. Do not add a test that instantiates either class.

- [ ] **Step 1: Add the mutex + snapshot accessor to the header**

In `software/wisp/src/WispConfig.h`, study `WispRoster.h`/`.cpp` first for the exact mutex-handle shim (`void* mutex_`, `asHandle`, ctor creates, dtor deletes), then add to `WispConfig`:

```cpp
 public:
  WispConfig();
  ~WispConfig();

  // Snapshot the manual palette as packed RGB into the caller's buffer
  // (needs maxColors*3 bytes). Returns the color count written. Lock-guarded:
  // the only manualPalette accessor safe to call off the loop task (the
  // StatusBeacon timer-service emit path).
  size_t copyManualPalette(uint8_t* outRgb, size_t maxColors) const;

 private:
  void* mutex_ = nullptr;   // SemaphoreHandle_t; shim mirrors WispRoster
```

- [ ] **Step 2: Implement the mutex lifecycle, guard the writer, implement the snapshot**

In `software/wisp/src/WispConfig.cpp`, mirror the `WispRoster` `asHandle` helper, then:

```cpp
WispConfig::WispConfig()  { mutex_ = xSemaphoreCreateMutex(); }
WispConfig::~WispConfig() { if (mutex_) vSemaphoreDelete(asHandle(mutex_)); }
```

Wrap the existing `.assign()` in `setManualPalette()` (the reallocation), keeping the NVS write outside the lock:

```cpp
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  manualPalette_.assign(colors.begin(), colors.begin() + n);
  xSemaphoreGive(asHandle(mutex_));
  // (existing NVS persist stays here, outside the lock)
```

Add the snapshot:

```cpp
size_t WispConfig::copyManualPalette(uint8_t* out, size_t maxColors) const {
  if (!out || !maxColors) return 0;
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  size_t n = std::min(manualPalette_.size(), maxColors);
  for (size_t i = 0; i < n; ++i) {
    out[i*3+0] = manualPalette_[i].r;
    out[i*3+1] = manualPalette_[i].g;
    out[i*3+2] = manualPalette_[i].b;
  }
  xSemaphoreGive(asHandle(mutex_));
  return n;
}
```

> Confirm `ManualPaletteColor`'s field names (`r`/`g`/`b`) and `setManualPalette`'s local variable names against the actual source; match them.

- [ ] **Step 3: Switch `emitPalette` to the snapshot and fix the comment**

In `software/wisp/src/StatusBeacon.cpp` `emitPalette()`, replace the unlocked `const auto& palette = config_->manualPalette();` iteration with a snapshot into a fixed local buffer sized to the palette cap (use the existing color-count constant the file already uses for the palette frame — confirm its name, e.g. `lamp_protocol::kMaxWispPaletteColors` or the local `WISP_PALETTE`-derived cap):

```cpp
  uint8_t rgb[kPaletteCap * 3];
  size_t count = config_->copyManualPalette(rgb, kPaletteCap);
  // ...feed rgb[i*3 + {0,1,2}] into the existing frame-build loop...
```

Delete the now-false threading comment (`"only mutated on the loop task, so a single critical section around the copy is enough"` — there was no critical section). Replace with one line stating the real invariant: the palette is snapshotted under `WispConfig`'s mutex before serialization. The oversize-truncation log is now redundant (the snapshot caps at `count`); drop it.

- [ ] **Step 4: Verify the firmware compiles**

Run: `npm run wisp:build`
Expected: SUCCESS.

- [ ] **Step 5: Native suite still green**

Run: `npm run wisp:test`
Expected: PASS — 42/42 (unchanged; this task adds no test).

- [ ] **Step 6: Commit**

```bash
git add software/wisp/src/WispConfig.h software/wisp/src/WispConfig.cpp software/wisp/src/StatusBeacon.cpp
git commit -m "fix(wisp): snapshot manual palette under a mutex for cross-task emit

emitPalette() (timer task) iterated WispConfig::manualPalette() unlocked while
setManualPalette() (loop task) reallocated it — a use-after-realloc. Add a
lock-guarded copyManualPalette snapshot mirroring CurrentPalette/WispRoster.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Convert CurrentPalette's spinlock to a mutex (heap alloc under portMUX)

**Severity: MEDIUM (highest-frequency exposure).** `CurrentPalette::update()` does `paletteId_ = p.id` (a `std::string` heap assign for non-SSO ids) inside `portENTER_CRITICAL` — illegal on ESP32 (malloc with interrupts disabled, starves the WiFi/ESP-NOW ISR), on every Aurora palette change. Fix: swap the `portMUX` for a FreeRTOS mutex (the `WispRoster` primitive), so the assign runs under a blocking lock with the scheduler enabled. Zero API change.

**Files:**
- Modify: `software/wisp/src/CurrentPalette.h` (replace portMUX member with a mutex handle + ctor/dtor; drop the `CURRENT_PALETTE_PORTMUX_*` macro block)
- Modify: `software/wisp/src/CurrentPalette.cpp` (`update` + `copyPaletteIdPrefix`: `portENTER/EXIT` → `xSemaphoreTake/Give`; fix comments)

**Interfaces:**
- Consumes: the `WispRoster` mutex idiom (`asHandle`, take/give); existing `CurrentPalette::update(...)`, `paletteId()`, `copyPaletteIdPrefix(...)` signatures stay byte-identical.
- Produces: nothing new — internal primitive swap only.

> No native test: `CurrentPalette.cpp` is excluded from the native build (its mux is FreeRTOS-only — the exclusion rationale is unchanged by spinlock→mutex). Verified by `npm run wisp:build`.

- [ ] **Step 1: Replace the portMUX member with a mutex handle**

In `software/wisp/src/CurrentPalette.h`: delete the `CURRENT_PALETTE_PORTMUX_TYPE/INIT/ENTER/EXIT` macro block and the `mutable ... mux_` member. Add a ctor/dtor declaration and:

```cpp
 private:
  void* mux_ = nullptr;   // SemaphoreHandle_t; shim mirrors WispRoster
```

Rewrite the header's threading comment (the `"copyPaletteIdPrefix() is portMUX-guarded"` lines) to state the real reason: a mutex (not a spinlock) guards `paletteId_` because the assignment heap-allocates and can't run under a spinlock.

- [ ] **Step 2: Create/delete the mutex and swap the lock calls**

In `software/wisp/src/CurrentPalette.cpp`: add the `asHandle` helper + ctor/dtor (`xSemaphoreCreateMutex` / `vSemaphoreDelete`). In `update()`:

```cpp
  xSemaphoreTake(asHandle(mux_), portMAX_DELAY);
  paletteId_ = p.id;
  xSemaphoreGive(asHandle(mux_));
```

Apply the same `Take/Give` swap in `copyPaletteIdPrefix()`. Fix the `update()` comment that claims the mux prevents a "torn `.data()`" — the real reason is the heap alloc.

- [ ] **Step 3: Verify the firmware compiles**

Run: `npm run wisp:build`
Expected: SUCCESS.

- [ ] **Step 4: Native suite still green**

Run: `npm run wisp:test`
Expected: PASS — 42/42.

- [ ] **Step 5: Commit**

```bash
git add software/wisp/src/CurrentPalette.h software/wisp/src/CurrentPalette.cpp
git commit -m "fix(wisp): use a mutex, not a spinlock, around the palette-id assign

paletteId_ = p.id heap-allocates for non-SSO ids; running it inside
portENTER_CRITICAL is illegal on ESP32 and starves the ESP-NOW ISR on every
palette change. Swap CurrentPalette's portMUX for a FreeRTOS mutex.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Replace ZoneSelector's heap vector with a fixed ring (heap alloc under portMUX)

**Severity: MEDIUM.** `ZoneSelector::observe()` does `std::vector` `erase(begin())` / `push_back()` inside `portENTER_CRITICAL(&observedMux_)` — heap ops under a spinlock. The set is hard-capped at `kMaxObservedZones = 16` `int`s, so a fixed `int[16]` + count is zero-heap and strictly better; **keep the portMUX** (it now guards only a bounded 16-int `memmove`, exactly what `MeshLink::peerMux_` legitimately does). `WispZoneSelector` is native-buildable, so this task gets a real unit test.

**Files:**
- Modify: `software/wisp/src/WispZoneSelector.h` (vector → fixed array + count; `observed()` → `observedCount()`; reword THREADING block; drop `<vector>`)
- Modify: `software/wisp/src/WispZoneSelector.cpp` (`observe` + `copyObserved` array logic; drop `<algorithm>`)
- Modify: `software/wisp/src/main.cpp` (the one `observed().size()` reader → `observedCount()`)
- Create: `software/wisp/test/test_zone_selector/zone_selector.cpp`

**Interfaces:**
- Consumes: existing `ZoneSelector::observe(int)`, `copyObserved(int* out, size_t outCap)`, `kMaxObservedZones` (16).
- Produces: `size_t ZoneSelector::observedCount() const` (replaces `const std::vector<int>& observed()`). FIFO + dedup-on-insert + oldest-eviction semantics are unchanged.

- [ ] **Step 1: Write the failing test**

Create `software/wisp/test/test_zone_selector/zone_selector.cpp`:

```cpp
#include <unity.h>
#include "WispZoneSelector.h"

void setUp() {}
void tearDown() {}

void test_dedup_and_fifo_cap() {
  wisp::ZoneSelector z;
  for (int i = 0; i < 20; ++i) z.observe(i);   // overflow the 16-cap
  int buf[16];
  size_t n = z.copyObserved(buf, 16);
  TEST_ASSERT_EQUAL_UINT(16, n);
  TEST_ASSERT_EQUAL_INT(4,  buf[0]);            // oldest 0..3 evicted
  TEST_ASSERT_EQUAL_INT(19, buf[15]);

  z.observe(10);                                // already present → no-op
  n = z.copyObserved(buf, 16);
  TEST_ASSERT_EQUAL_UINT(16, n);                // no growth, no reorder
  TEST_ASSERT_EQUAL_INT(19, buf[15]);
}

void test_copyObserved_respects_outCap() {
  wisp::ZoneSelector z;
  for (int i = 0; i < 8; ++i) z.observe(i);
  int buf[4];
  size_t n = z.copyObserved(buf, 4);
  TEST_ASSERT_EQUAL_UINT(4, n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dedup_and_fifo_cap);
  RUN_TEST(test_copyObserved_respects_outCap);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test against the current vector implementation**

Run: `npm run wisp:test`
Expected: PASS — these assertions describe the existing FIFO/dedup/cap behavior, so they lock it in *before* the refactor. (If `observe()` is missing dedup or eviction in some edge, the test will reveal it — investigate before refactoring.)

- [ ] **Step 3: Refactor the header to a fixed array**

In `software/wisp/src/WispZoneSelector.h`: drop `#include <vector>`; replace `const std::vector<int>& observed() const` with `size_t observedCount() const { return observedCount_; }`; replace the member with:

```cpp
  int    observedZones_[kMaxObservedZones];
  size_t observedCount_ = 0;
```

Reword the THREADING block: the vector-relocation hazard is gone; the FIFO shift is now a bounded array `memmove`, and the portMUX only prevents `copyObserved` seeing a half-shifted buffer.

- [ ] **Step 4: Refactor `observe` + `copyObserved`**

In `software/wisp/src/WispZoneSelector.cpp` (drop `#include <algorithm>`), rewrite `observe`:

```cpp
void ZoneSelector::observe(int zone) {
  WISP_ZONE_PORTMUX_ENTER(&observedMux_);
  bool present = false;
  for (size_t i = 0; i < observedCount_; ++i)
    if (observedZones_[i] == zone) { present = true; break; }
  if (!present) {
    if (observedCount_ >= kMaxObservedZones) {       // oldest-out FIFO
      memmove(observedZones_, observedZones_ + 1,
              (kMaxObservedZones - 1) * sizeof(int));
      observedCount_ = kMaxObservedZones - 1;
    }
    observedZones_[observedCount_++] = zone;
  }
  WISP_ZONE_PORTMUX_EXIT(&observedMux_);
}
```

Update `copyObserved` to iterate `observedCount_` / `observedZones_[i]` instead of the vector. Keep `<cstring>` for `memmove`.

- [ ] **Step 5: Fix the one external reader**

In `software/wisp/src/main.cpp`, change the `selector.observed().size()` log site (≈ line 529) to `selector.observedCount()`. Grep to confirm it's the only `.observed()` caller: `git grep -n '\.observed()' software/wisp`.

- [ ] **Step 6: Run the test against the refactor + build**

Run: `npm run wisp:test`
Expected: PASS — same assertions, now against the fixed-array implementation (42 + 1 new env). Then `npm run wisp:build` → SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add software/wisp/src/WispZoneSelector.h software/wisp/src/WispZoneSelector.cpp software/wisp/src/main.cpp software/wisp/test/test_zone_selector
git commit -m "fix(wisp): fixed-array observed-zones ring (no heap under the spinlock)

observe() did vector erase/push_back inside portENTER_CRITICAL. The set is
hard-capped at 16 ints, so a fixed array + count is zero-heap; the portMUX now
guards only a bounded memmove. Add a native test pinning FIFO/dedup/cap.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Guarantee the wispStatus JSON stays under the 230-byte cap

**Severity: MEDIUM.** The wispStatus JSON's hand-computed "224 B, under 230" budget predates two added fields now emitted (`source` +17, `offColor` +24). Measured worst case is ~267 B and the realistic fleet case (~6 observed zones) already crosses 230 — at which point the `jsonLen > CONTROL_MAX_PAYLOAD` guard **silently drops the broadcast** and the app stops seeing status. Both `source` and `offColor` are consumed by the Flutter parser (`wisp_status.dart`), so neither can be dropped. Fix: extract a native-testable JSON builder that adds observed zones greedily and stops before crossing the cap — a by-construction bound that drops only overflow zones and preserves every field. Plus harden `zoneId` to a sane range at its trust boundaries (it's persisted to NVS and rendered into this JSON).

**Files:**
- Create: `software/wisp/src/status_json.hpp`, `software/wisp/src/status_json.cpp`
- Modify: `software/wisp/src/StatusBeacon.cpp` (`emitStatus` → build `WispStatusFields`, call the new builder; delete the stale worst-case tally comment)
- Modify: `software/wisp/src/WispConfig.cpp` (`setSelectedZone`: reject `> kMaxZoneId`)
- Modify: `software/wisp/src/WispZoneSelector.h`/`.cpp` (`kMaxZoneId`, `isValidZone`, guard `observe` + the three current-zone setters)
- Create: `software/wisp/test/test_status_json/test_status_json.cpp`
- Modify: `docs/dev/mesh-api.md` (add `source` + `offColor` to the wispStatus envelope example; note observed-zones-fit-to-budget)

**Interfaces:**
- Consumes: `CONTROL_MAX_PAYLOAD` (230), ArduinoJson v7 (`JsonDocument`, `measureJson`, `serializeJson`), the existing snapshots `emitStatus` already gathers (currentZone, zoneSource string, observed ints, wifi/aurora bools, paletteIdPrefix, lastSeenMs, source string, offColor).
- Produces:
  - `wisp::WispStatusFields` (POD: `int currentZone; const char* zoneSource; const int* observedZones; size_t observedCount; bool wifiConnected, auroraConnected; const char* paletteIdPrefix; uint32_t lastSeenMs; const char* source; uint8_t offR, offG, offB; bool hasOffColor;`)
  - `size_t wisp::buildWispStatusJson(const WispStatusFields& f, char* out, size_t outCap, size_t cap)` → serialized length, always `≤ cap`, or 0 on failure.
  - `wisp::kMaxZoneId` (9999) and `bool wisp::isValidZone(int)` in `WispZoneSelector.h`.

- [ ] **Step 1: Write the failing test for the JSON builder**

Create `software/wisp/test/test_status_json/test_status_json.cpp`:

```cpp
#include <unity.h>
#include <ArduinoJson.h>
#include "status_json.hpp"

constexpr size_t CAP = 230;

void setUp() {}
void tearDown() {}

void test_worst_case_stays_under_cap() {
  int zones[16];
  for (int i = 0; i < 16; ++i) zones[i] = 2147483647;   // pathological width
  wisp::WispStatusFields f{ 2147483647, "firstSeen", zones, 16,
                            true, true, "abcdef12", 4294967295u, "aurora",
                            255, 255, 255, true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= CAP);                            // the guarantee
  JsonDocument d;
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(d, out));
  TEST_ASSERT_TRUE(d["observedZones"].as<JsonArray>().size() < 16); // cap engaged
  TEST_ASSERT_FALSE(d["source"].isNull());              // field preserved
  TEST_ASSERT_FALSE(d["offColor"].isNull());            // field preserved
}

void test_empty_observed_fits() {
  wisp::WispStatusFields f{ 3, "appOp", nullptr, 0,
                            false, false, "abcdef12", 1000u, "off",
                            10, 20, 30, true };
  char out[256];
  size_t n = wisp::buildWispStatusJson(f, out, sizeof(out), CAP);
  TEST_ASSERT_TRUE(n > 0 && n <= CAP);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_worst_case_stays_under_cap);
  RUN_TEST(test_empty_observed_fits);
  return UNITY_END();
}
```

> `[env:native]` in `software/wisp/platformio.ini` already declares `bblanchon/ArduinoJson` and `test_build_src=yes` (the existing `test_palette_parse` proves the combo works). Confirm the exact ArduinoJson deserialize-error idiom against that test's usage.

- [ ] **Step 2: Run to verify it fails (builder doesn't exist)**

Run: `npm run wisp:test`
Expected: FAIL — `status_json.hpp` not found / `buildWispStatusJson` undefined.

- [ ] **Step 3: Implement the JSON builder**

Create `software/wisp/src/status_json.hpp` declaring `WispStatusFields` and `buildWispStatusJson` (signatures above). Create `software/wisp/src/status_json.cpp`:

```cpp
#include "status_json.hpp"
#include <ArduinoJson.h>

namespace wisp {

size_t buildWispStatusJson(const WispStatusFields& f, char* out,
                           size_t outCap, size_t cap) {
  JsonDocument doc;
  doc["char"]            = "wispStatus";
  doc["currentZone"]     = f.currentZone;
  doc["zoneSource"]      = f.zoneSource;
  JsonArray z            = doc["observedZones"].to<JsonArray>();
  doc["wifiConnected"]   = f.wifiConnected;
  doc["auroraConnected"] = f.auroraConnected;
  doc["paletteIdPrefix"] = f.paletteIdPrefix;
  doc["lastSeenMs"]      = f.lastSeenMs;
  doc["source"]          = f.source;
  if (f.hasOffColor) {
    JsonArray o = doc["offColor"].to<JsonArray>();
    o.add(f.offR); o.add(f.offG); o.add(f.offB);
  }
  // ponytail: O(n * measureJson), n <= 16 — trivial, and it gives a
  // by-construction guarantee the serialized doc never exceeds cap.
  for (size_t i = 0; i < f.observedCount; ++i) {
    z.add(f.observedZones[i]);
    if (measureJson(doc) > cap) { z.remove(z.size() - 1); break; }
  }
  size_t n = serializeJson(doc, out, outCap);
  return (n > 0 && n <= cap) ? n : 0;
}

}  // namespace wisp
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `npm run wisp:test`
Expected: PASS — the new `test_status_json` env is green, including the pathological worst case `≤ 230`.

- [ ] **Step 5: Wire `emitStatus` to the builder**

In `software/wisp/src/StatusBeacon.cpp` `emitStatus()`: build a `WispStatusFields` from the snapshots it already gathers, then `size_t jsonLen = wisp::buildWispStatusJson(fields, jsonBuf, sizeof(jsonBuf), lamp_protocol::CONTROL_MAX_PAYLOAD);`. Keep the existing `if (jsonLen == 0) return;` (now only an alloc-fail/belt-and-suspenders path — it can no longer be hit by zone count). Delete the stale worst-case tally comment block.

- [ ] **Step 6: Add the zoneId guard**

In `software/wisp/src/WispZoneSelector.h`, add `constexpr int kMaxZoneId = 9999;` and `inline bool isValidZone(int z) { return z >= 0 && z <= kMaxZoneId; }`. In `WispZoneSelector.cpp`, early-return (drop) out-of-range zones in `observe()`, `latchFirstSeen()`, `setFromOp()`, `setFromNvs()`. In `WispConfig.cpp` `setSelectedZone()`, extend the existing `< 0` reject to also reject `> kMaxZoneId` (this is the NVS-persist / app-`setZone` boundary; the dispatcher already defers to storage here, so no dispatcher change).

> `kMaxZoneId = 9999` (4 digits) is generous beyond any real venue's stage-index count and bounds the JSON's `currentZone`/observed widths, restoring real margin on top of the by-construction cap.

- [ ] **Step 7: Build, full suite, doc update**

Run: `npm run wisp:build` → SUCCESS. Run: `npm run wisp:test` → PASS (all envs). Then update `docs/dev/mesh-api.md`'s wispStatus envelope example to include `source` and `offColor`, and add a one-line note that observed zones are truncated to fit `CONTROL_MAX_PAYLOAD`.

- [ ] **Step 8: Commit**

```bash
git add software/wisp/src/status_json.hpp software/wisp/src/status_json.cpp software/wisp/src/StatusBeacon.cpp software/wisp/src/WispConfig.cpp software/wisp/src/WispZoneSelector.h software/wisp/src/WispZoneSelector.cpp software/wisp/test/test_status_json docs/dev/mesh-api.md
git commit -m "fix(wisp): keep wispStatus JSON under the 230-byte cap by construction

Added source/offColor fields pushed the worst case to ~267B; past ~6 observed
zones the >230 guard silently dropped the whole broadcast. Extract a
native-testable builder that adds observed zones greedily and stops before the
cap, preserving every app-consumed field; clamp zoneId to 0..9999 at its NVS
and mesh trust boundaries.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Bound Aurora inflate output (zip bomb) + fix the millis()-wrap eviction

**Severity: HIGH (zip bomb) + LOW (wrap).** `inflateInto` appends every 32 KB inflate block into `out` with no ceiling — a crafted high-ratio deflate frame from the unauthenticated Aurora WebSocket expands a tiny payload to hundreds of MB → `bad_alloc`/abort on the 512 KB C6. The exact legitimate ceiling is the nanopb-generated `aurora_NotificationEnvelope_size` (2251) — anything larger can't be a valid envelope. Separately, `prune` in `LampInventory.cpp` and `WispRoster.cpp` computes `nowMs - lastSeenMs` unguarded; when a HELLO stamps `lastSeenMs` slightly ahead of the loop-top `nowMs` sample (the documented resample race), it underflows and evicts a fresh lamp. `main.cpp:518` already guards the identical shape — mirror it.

**Files:**
- Modify: `software/wisp/src/aurora/Compression.h`, `software/wisp/src/aurora/Compression.cpp` (thread a `maxOut` ceiling through `maybeInflate`/`inflateInto`)
- Modify: `software/wisp/src/aurora/NotificationCodec.cpp` (pass `aurora_NotificationEnvelope_size`)
- Modify: `software/wisp/src/LampInventory.cpp`, `software/wisp/src/WispRoster.cpp` (guarded subtraction)
- Modify: `software/wisp/test/test_notification_codec/test_notification_codec.cpp` (oversized-inflate reject case)
- Modify: `software/wisp/test/test_wisp_roster/test_wisp_roster.cpp` (lastSeen-ahead-of-now case)

**Interfaces:**
- Consumes: `aurora_NotificationEnvelope_size` (from `aurora/proto/aurora_notifications.pb.h`), existing `Compression::maybeInflate`, `NotificationCodec::decode`, `WispRoster::recordPeerClaim`/`recomputeClaims`.
- Produces: `Compression::maybeInflate(const uint8_t* d, size_t n, size_t maxOut, std::vector<uint8_t>& out)` and `inflateInto(..., size_t maxOut, ...)` — both return false when output would exceed `maxOut`.

- [ ] **Step 1: Write the failing zip-bomb test**

In `software/wisp/test/test_notification_codec/test_notification_codec.cpp`, add (and `RUN_TEST` it in `main`):

```cpp
#include "miniz.h"   // vendored alongside Compression.cpp

void test_rejects_oversized_inflate(void) {
  std::vector<uint8_t> big(64 * 1024, 0);     // highly compressible
  std::vector<uint8_t> z(1024);
  mz_ulong zlen = z.size();
  TEST_ASSERT_EQUAL(MZ_OK, mz_compress(z.data(), &zlen, big.data(), big.size()));
  DecodedNotification d = NotificationCodec::decode(z.data(), zlen);
  TEST_ASSERT_FALSE(d.ok);   // dropped, not decoded; no crash / no OOM
}
```

> Confirm `DecodedNotification`'s success field name (`ok`) and the `decode` signature against the source. If linking `mz_compress` is awkward in the native env, hand-craft a small raw-deflate run-length stream that expands past 2251 — same assertion.

- [ ] **Step 2: Run to verify it fails**

Run: `npm run wisp:test`
Expected: FAIL — current `decode` inflates the bomb and returns `ok=true` (or OOMs). This proves the gap.

- [ ] **Step 3: Thread the ceiling through Compression**

In `software/wisp/src/aurora/Compression.h`/`.cpp`, add a `size_t maxOut` param to `maybeInflate` and `inflateInto`. In the `inflateInto` append loop, after each `out.insert(...)`:

```cpp
  if (out.size() > maxOut) return false;   // zip-bomb guard
```

In `maybeInflate`, forward `maxOut` to every `inflateInto` call, and bound the uncompressed passthrough too:

```cpp
  if (n > maxOut) return false;
  out.assign(d, d + n);
  return true;
```

- [ ] **Step 4: Pass the envelope ceiling from the codec**

In `software/wisp/src/aurora/NotificationCodec.cpp`:

```cpp
  if (!Compression::maybeInflate(frame, len,
                                 aurora_NotificationEnvelope_size, raw))
    return out;   // oversized/garbage → dropped frame
```

Confirm `aurora_NotificationEnvelope_size` is visible (it's in the generated `aurora_notifications.pb.h` the codec already includes).

- [ ] **Step 5: Run the zip-bomb test to verify it passes**

Run: `npm run wisp:test`
Expected: PASS — `decode` now returns `ok=false` for the bomb, no OOM.

- [ ] **Step 6: Write the failing wrap-underflow test**

In `software/wisp/test/test_wisp_roster/test_wisp_roster.cpp`, add (and `RUN_TEST`) a case that stamps a peer's `lastSeenMs` *ahead* of the prune's `nowMs` (mirror the existing `test_peer_silence_ages_out_and_we_adopt` setup for the exact helper/signature names):

```cpp
void test_peer_not_pruned_when_lastseen_ahead_of_now(void) {
  wisp::WispRoster r;
  r.setSelfMac(kSelfMac);
  uint8_t entries[7];
  packEntry(entries, kLampX, -65);
  r.recordPeerClaim(kPeerHighMac, entries, 1, /*nowMs=*/5000);
  auto lamps = obs(kLampX, -75);                  // peer is closer → owns X
  r.recomputeClaims(&lamps, 1, /*nowMs=*/4000);   // now < lastSeen by 1s
  TEST_ASSERT_FALSE(r.claims(kLampX));            // buggy code prunes & adopts
}
```

- [ ] **Step 7: Run to verify it fails**

Run: `npm run wisp:test`
Expected: FAIL — `4000 - 5000` underflows to ~4.29e9 > age limit → peer pruned → we wrongly claim X → `claims(kLampX)` is true.

- [ ] **Step 8: Apply the guarded subtraction at both sites**

Mirror `main.cpp:518` exactly. In `software/wisp/src/WispRoster.cpp` (≈ line 232):

```cpp
  const uint32_t last = peers_[i].lastSeenMs;
  const uint32_t age  = (nowMs >= last) ? nowMs - last : 0;
  if (age > WISP_ROSTER_PEER_AGE_MS) {
```

In `software/wisp/src/LampInventory.cpp` (≈ line 84):

```cpp
  const uint32_t last = entries_[i].lastSeenMs;
  const uint32_t age  = (nowMs >= last) ? nowMs - last : 0;
  if (last != 0 && age > maxAgeMs) {
```

> Inline guard, not a shared helper: the codebase's convention is inline `nowMs >=` guards (main.cpp:518 and the firmware-distributor sites). A new util TU for three one-liners fails the laziness / no-speculative-surface test. Promote to a helper only if a 4th site appears.

- [ ] **Step 9: Run both tests + build**

Run: `npm run wisp:test`
Expected: PASS — both new cases green, all prior green. Run: `npm run wisp:build` → SUCCESS.

- [ ] **Step 10: Commit**

```bash
git add software/wisp/src/aurora/Compression.h software/wisp/src/aurora/Compression.cpp software/wisp/src/aurora/NotificationCodec.cpp software/wisp/src/LampInventory.cpp software/wisp/src/WispRoster.cpp software/wisp/test/test_notification_codec software/wisp/test/test_wisp_roster
git commit -m "fix(wisp): cap Aurora inflate output + guard millis()-wrap eviction

Bound inflateInto to aurora_NotificationEnvelope_size (2251) so a crafted
high-ratio deflate frame from the unauthenticated Aurora WS can't OOM the C6.
Guard nowMs-lastSeenMs in LampInventory/WispRoster prune, mirroring main.cpp,
so a HELLO stamped ahead of the loop-top sample can't evict a fresh lamp.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Out of scope (report, not fix)

The audit also surfaced ~50 lines of dead/speculative code and a dozen banned-comment cleanups (write-only `LampInventory` color fields, `AuroraPaletteClient::setStaticHost`, `WispConfig::clearWifi`, `WispRoster::peerCount`, `CurrentPalette::lastChangeMs`, unread `PaletteList` fields; date-archaeology/forward-leaning comments). These are hygiene, not "get it working again" — track separately (e.g. a `ponytail-audit` follow-up). The `mesh-api.md` "37-byte HELLO" staleness and the missing `HELLO_TLV_FS_STATE` mirror entry are doc/drift nits; the JSON-field doc gap is folded into Task 5.

## Verification (whole-plan)

After Task 6: `npm run wisp:test` (expect ~48 cases green: 40 baseline + Task 1's 2 + Task 4's 2 + Task 5's 2 + Task 6's 2), `npm run wisp:build` clean, and `npm run lamp:test` unaffected. Hardware bench-verify (out of the native suite's reach): flash a wisp via `npm run wisp:flash`, tail it next to a lamp with `scripts/bench_tap.py`, and confirm the lamp's inventory now shows the wisp (HELLO received), the app's wisp pane renders status, and a zone pick round-trips — that's the real proof Task 1 revived it.
```
