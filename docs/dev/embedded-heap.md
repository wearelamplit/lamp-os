# Embedded heap discipline

`malloc(N)` needs N *contiguous* bytes, so the number that bites is the largest
free block, not total free. Two figures matter and they measure different things:

- **Settled-boot headroom** (idle, no BLE client): free heap ~46.5 KB,
  fragmented to a ~40.9 KB largest contiguous block, with BLE, ESP-NOW, and wifi
  resident. This is the quiet-desk best case.
- **Runtime largest free block** (BLE client connected + a full roster): the
  largest contiguous block craters to **~23.5 KB**. This is the real cap on
  roster / per-peer / hot-path growth, and the number to design against.

A whole column of independent bugs turned out to be one anti-pattern hitting the
runtime ceiling. Read this before adding any feature that snapshots the roster,
tracks per-peer state, or answers "who's nearby." Measure with the probe (below)
on the bench under real BLE + fleet load; the quiet-desk figure hides the cliff.

The softAP webapp and BLE are heap-exclusive: a config client connecting can
squeeze free heap into a range too tight for the AsyncWebServer to serve the
config page, so `ble_control::deinitForWebapp()` tears BLE (NimBLE host + BT
controller) down on client-connect to give the serve headroom. BLE comes back
only via a reboot on session end (`webapp::tick()`), never an in-place re-init.

## The recurring anti-pattern

Fleet-awareness features (crowd-dim, claims, nearby lists, greetings, personality)
all answer "who's around me." The convenient implementation snapshots the roster
into a fresh `std::vector`, one `std::string` per entry, then sorts it. That is
fine on a laptop and lethal here, and each feature reinvented it. `personality_engine`
ran it on every main-loop tick (kHz); the wisp ran it at ~200 Hz. They were the top
fragmentation drivers on the heap.

Two properties make it lethal:

- **kHz alloc/free churn of variable-size blocks fragments a no-compaction heap.**
  The vector and its N strings allocate and free every pass. Nothing moves once
  placed, so a long-lived allocation that lands in a freed gap keeps that gap from
  merging back. The largest contiguous block erodes and stays eroded. It does not
  even recover when the triggering BLE client disconnects, because the churn never
  stops.
- **A 17-char lampId (`"AA:BB:CC:DD:EE:FF"`) defeats std::string SSO** (the inline
  buffer holds ~15 chars). Every address becomes a separate heap allocation.

## Rules

1. **Watch `largest`, not just `free`.** `heap_caps_get_largest_free_block()` is the
   real budget. `util/heap_probe.hpp` (`lamp::logHeap`, LAMP_DEBUG) logs both at
   boot / mesh / webapp / ble-connect / ble-disconnect / ble-pre / ble-post /
   ota-stream / web-save. Tap the serial, grep `[heap]`, find the checkpoint that
   craters the block. Measure the fragmenter; do not theorize it.
2. **No heap churn on the hot path.** Never rebuild-and-sort a fresh allocation per
   loop tick. Throttle it, dirty-gate it (rebuild only when the roster changed), or
   reuse a member buffer `reserve()`d once to its max. A rotating configurator,
   the resting state of a lamp whose colours flow, draws every frame without
   allocating (`test_configurator_rotation` counts zero allocations over 1,000
   rotating draws); the loop pattern is built once per colour or rotation change.
3. **No per-entry `std::string` for addresses or short keys.** Use a fixed
   `char[18]` or the raw 6-byte address. Anything past ~15 chars heap-allocates.
4. **Scan in place, do not snapshot.** "Closest Smitten peer", "nearest by RSSI",
   "count of X": iterate the roster where it lives, allocation-free. Do not
   materialize a sorted copy to read one value off it.
5. **Front-load permanents; preallocate.** Long-lived allocations belong at boot,
   while the heap is still one contiguous slab, `reserve()`d to their max, so they
   pack low instead of bisecting the runtime free region.
6. **No heavy work under a spinlock or on the BLE task.** A JSON build inside
   `portENTER_CRITICAL` masks interrupts for milliseconds; a 10 KB JSON built on the
   NimBLE host task competes with the largest free block. Pre-build on a Core-1
   cache and let the mux cover only the pointer swap. Every section payload works
   this way except `exprcat`, which is generated into `.rodata` at build time and
   served by reference, so it never builds on the host task at all; match one or
   the other. `wispstatus` is the standing exception: it builds its merge (well
   under 1 KB) on the host task, the same build the direct `CHAR_WISP_STATUS`
   read has always run there.
7. **Prefer a shared roster view over a new snapshot.** "Who's around" gets asked
   from many features, so a single shared roster view earns its keep. Do not let
   the next feature roll its own vector-of-strings copy. The shared views are
   `snapshotNear`, `getMesh`, and `getAll`. Each copies the matching entries
   under the roster mutex into a member vector of a slim, trivially copyable
   projection holding only the fields its callers read (`NearbyCopy`,
   `MeshPeer`, `ListedPeer`, a fraction of `RosterEntry`'s size), then filters
   or sorts outside the lock, which keeps the critical section short enough for
   the ESP-NOW receive path's bounded takes. The near and mesh buffers are
   `reserve()`d to `kCapacity` (50) when the roster is constructed at boot, so
   a growing roster never reallocates them. The `getAll` buffer serves only the
   app-facing nearby list: it is reserved to `kCapacity` in one allocation on
   the first build after an app or webapp client connects and released when the
   last one disconnects, and the build skips (logging OOM) if that reservation
   throws. A new roster consumer reads one of these views, adding a field to its
   projection if needed, and never takes a full `RosterEntry` copy per peer.

   The buffers are **single-task and non-reentrant**: loop task (Core 1) only,
   and a reference is invalidated by the next call to the same getter.
   `LAMP_DEBUG` asserts both (unexpected task, re-entrant fill). A path that
   hands each peer to a **user callback** walks the `snapshotNear` reference: a
   callback re-entering `getMesh`/`getAll` fills a different buffer and cannot
   invalidate the walk. The one uncovered case is a nested `snapshotNear` that
   rebuilds (a `forEachNearby` or `crowd()` inside a near-walk callback) and
   overwrites the buffer mid-walk; no near-walk nests today. `snapshotNear` also
   caches: repeated calls with the same `maxAgeMs` within `kSnapshotCacheMs`
   (1 s) return the existing copy without re-locking or sorting, so a behavior
   calls it per frame and does not roll its own throttle. A different
   `maxAgeMs` rebuilds.

8. **A save allocates once, exactly, and only when the heap can spare it.** A
   large document serialized into an Arduino `String` or `std::string` grows by
   repeated reallocation, a staircase of blocks on an already fragmented heap,
   and a failed `String` reserve leaves a null buffer that NVS then
   dereferences. `util/heap_guard.hpp` holds the pattern: `heapCanSpare(n)`
   asks whether the largest free block holds `n` plus `kHeapHeadroom` (4 KB, so
   the radio stacks still find their buffers right after); `allocateSpare(n)`
   is the nothrow allocation behind it; `serializeGuarded(doc, len)` measures
   the document, refuses one that overflowed while it was built (it would save
   truncated), and serializes into one exactly-sized buffer. A refused save
   never aborts: it logs, keeps the dirty state and retries later.
   A config save takes no config-sized heap, so it never waits on a large free
   block (NVS itself allocates a handle per call and small bookkeeping per
   page): the expression records and knockout are written from stack buffers, and the
   JSON streams through `JsonWriter` (`util/json_writer.hpp`, compact JSON
   straight into a fixed buffer, byte-identical to ArduinoJson) into a
   1792-byte buffer reserved in .bss (`kMaxStoredCfgJsonLen`, above both a
   whole config's 1759-byte ceiling and the split rest's 1746-byte worst case
   with every free-text field fully escaped and every number at its widest).
   A whole web document is parsed once, on the loop: the web PUT gathers the
   body (at most 16 KB, `kMaxSettingsBody`; a lamp at every cap sends about
   12.9 KB) into one block and hands that block over without parsing it, and
   `saveRawJson` parses it only when it is strict JSON and the heap can take
   it. ArduinoJson also reads single-quoted strings and unquoted keys, which
   the scan cannot measure, so a body with a `'` outside a double-quoted
   string, a bare word that is not `true`, `false` or `null`, or a key that
   is not double-quoted is refused (400); the web UI sends strict JSON. Free
   heap: 15 bytes per structural token (`{`, `[`, `:`, `,`), a byte per
   document byte, 24 per distinct string (ArduinoJson stores each distinct
   key or value once; the scan compares each short string with the last 32),
   4 per byte of the longest string (built by doubling, so up to twice it,
   then kept and copied into the model) and 4 KB for the model, which carries
   some slack. A parse that runs out of memory all the same answers 500. Largest
   block: a 4 KB slot pool, or twice the longest string plus 128 bytes.
   Those are a counting allocator's host figures with margin
   (`test_config_persist_validate`: a 5.4 KB document peaks at 16 KB, the
   standard variant's 18 full expressions at 23 KB, a lamp at every cap at
   32 KB, 16 KB of bare numbers at 136 KB, of distinct four-character strings
   at 86 KB as values and 97 KB as keys, one 16 KB string at 53 KB with a
   16 KB block, and one of 8,193 bytes, just past a doubling, at 37 KB with a
   16 KB block), and the host's 64-bit slots are twice the ESP32's. With
   the body taken out of 46.5 KB free and a 23.5 KB largest block, the
   typical document and the 18 expressions pass; a setup near the caps may
   be refused by that check (500) and saves from the app instead. These are
   host figures; the device's need a bench check. The dispositions flush
   writes its blob from the stack, and its JSON for a reader is allocated
   outside its lock. A failed save (a full NVS, or a JSON that outgrew its
   buffer) backs off 5 s doubling to 60 s (`util/retry_backoff.hpp`) for
   both, and the commit drain waits on the same backoff, so no failure path
   retries at loop rate. Old stored formats load unchanged and move to the
   current keys on the first
   save. Do not lean on `try`/`catch (std::bad_alloc)` alone: the build sets
   `CONFIG_CXX_EXCEPTIONS_EMG_POOL_SIZE=0`, so on a truly exhausted heap the
   throw itself cannot allocate and ends in `terminate()`.

## The NVS budget

The `nvs` partition is 20 KB (0x5000, five 4 KB pages) and OTA cannot change
the partition table. NVS keeps one page free for garbage collection, so four
pages of 126 32-byte entries hold everything: 504 entries. A string costs a
header entry plus its bytes with the NUL; a blob adds an index entry. Entries
are only appended to a page, and an erase frees nothing until garbage
collection, which copies the live entries of the page with the fewest into
the free page and frees it, one page per request. Rewriting an item needs room
for the new copy before the old one is erased.

Two placement rules make the entry count alone overstate the room
(`nvs_storage.cpp`, `nvs_pagemanager.cpp`, `nvs_page.cpp`):

- **A string must fit one page.** When the active page has no room, NVS asks
  for a new page once and fails `NOT_ENOUGH_SPACE` if that page has no room
  either. Collection empties the page with the fewest live entries, which
  holds at most a quarter of the `L` live entries on the four data pages, so
  a string of `S` entries is always placed while `L <= 4 x (126 - S) + 3`.
- **A blob under 400 bytes behaves like a string.** A blob's first chunk
  goes into the active page's tail only when that tail holds the whole blob
  or at least 400 bytes (`CHUNK_MAX_SIZE / 10`); otherwise NVS asks for a
  fresh page, and fails if its tail is no longer. A blob under 400 bytes so
  always lands as one chunk in one page's tail. A larger one starts in a tail
  of 400 bytes or more and continues in chunks across pages, one header
  entry per chunk, collecting a page for each.
- **A failed blob rewrite can take the old copy.** IDF 5.5's
  `writeMultiPageBlob` cleans up a write that fails after a chunk (a page
  request that fails, or the 12-byte `UsedPageNode` allocation) by erasing,
  on each page it wrote, the chunk numbered by its position (0, 1, ...)
  rather than by the new version's offset. Rewriting a blob whose live
  version starts at offset 0 can so erase the live version's chunk; the first
  read then finds the chunk missing and erases the whole blob
  (`readMultiPageBlob`), as the boot after a reset does
  (`eraseMismatchedBlobIndexes`). The rest blob therefore alternates between
  `cfgb` and `cfgc`, written to the one that is not live after erasing it
  first, so a failed write never reaches the live copy; with both present (a
  save cut short before the older is erased) `cfgb` is the live one, for
  saves and loads alike. The count is a string, which a failed write leaves
  as it was. A record, `ko` or `disp` rewrite can still
  lose its old copy that way, only when NVS has no page for it or the heap
  cannot spare 12 bytes: the save fails and its retry writes it back from
  RAM, and a reset before then drops that one record (logged at load), the
  knockout levels or the dispositions.

`config_codec::saveConfig` writes one of two shapes, chosen per save:

- **Whole**, up to 1759 bytes (`kMaxWholeCfgLen`, 56 entries): one `cfg`
  string that carries the expressions and the knockout levels too. Every
  firmware reads this form, so a lamp flashed back to an older release keeps
  everything. Two expressions with every colour and parameter at its cap
  fit; smaller ones fit more.
- **Split**, for anything bigger. Every value but one small string is a blob,
  at the caps (`config/config_types.hpp`):

| Key | Content | Worst case | Entries |
|---|---|---|---|
| `x0`..`x20` | one expression each, compact record (`encodeExpression`) | 121 B | 21 x 6 = 126 |
| `xn` | expression count, a decimal string | 2 B | 2 |
| `ko` | knockout level per pixel | 255 B | 10 |
| `cfgb` or `cfgc` | base, shade and home mode as JSON (`writeStoredJson` without the lamp section) | 1746 B fully escaped | 57 |
| `cfg` | the lamp section as a JSON string, the one an older release reads | 376 B fully escaped | 13 |
| `cfgl` | the lamp section, staged while `cfg` is replaced | 376 B | 14 while it exists |
| `disp` | dispositions, `[mac:6][value:1]` per peer | 701 B | 24 |
| `lampType` | variant id | under 16 B | 2 |

Outside it, 171 entries (`systemKeys` in `test_config_records`):

- 5 namespace entries: `lamp`, `crash`, `phy`, `nvs.net80211`, `nimble_bond`.
- PHY calibration: a 1904-byte blob (62), a 6-byte blob (3), a u32 (1): 66.
- The WiFi driver's namespace: empty. The lamp keeps WiFi config in RAM and
  erases that namespace at boot.
- The `crash` namespace: the crash record (`last`, an 80-byte blob, 5) and the
  boot history (`boots`, a 64-byte blob, 4): 9.
- NimBLE's bond store (`ble_store_nvs.c`, `nimble_bond`; bonding and
  host-based privacy are on, so legacy bonds may be present): with
  `MAX_BONDS` 3, `MAX_CCCDS` 8 and `MAX_CSFCS` 1 (the connection count),
  `our_sec_N` and `peer_sec_N` blobs of `ble_store_value_sec` (88 bytes on
  the ESP32, 5 entries each) for 3 bonds is 30; `p_dev_rec_N` blobs of
  `ble_hs_dev_records` (44 bytes, 4 each) for 4 is 16; `cccd_sec_N` (16
  bytes, 3 each) for 8 is 24; `csfc_sec_N` (8 bytes) 3; `local_irk_N` and
  `rpa_rec_N` (under 32 bytes, 3 each) for 3 each is 18. Total 91.

The arithmetic, every case with at least 29 entries to spare
(`test_config_records`, figures for quote- or backslash-filled text):

- **Whole string.** Live while it is rewritten: 171 + 2 + 24 + its old copy
  or staged blob 57 = 254. The bound for 56 entries is 4 x 70 + 3 = 283:
  29 spare.
- **Lamp string.** Live beside every split key at its cap: 171 + 2 + 24 +
  126 + 2 + 10 + 57 + its old copy or `cfgl` 14 = 406. The bound for 13 is
  455: 49 spare.
- **Small blobs.** The largest is a dispositions blob just under 400 bytes,
  14 entries, at 405 live: bound 451, 46 spare. Records (6), `ko` (10) and
  `cfgl` (14) are no larger.
- **Entries.** The split store holds 405; rewriting its largest blob, PHY
  calibration (62) or the rest (57), peaks at 467 of 504. Switching from
  whole peaks at 462 (every split key, the staged lamp section and the new
  rest blob beside the whole string), and so does staging a whole config
  beside the split store.
- **The first save after an update.** An older release's `cfg` string can
  hold 4,000 bytes (127 entries), beside its dispositions JSON (about 2.2 KB,
  71 entries, migrated to the 24-entry blob at the first load). The first
  split save keeps that string until the new keys land, but those keys copy
  only what the string held: a string full of fully escaped rest leaves room
  for few expressions, and one full of expressions has a short rest. Over
  every legacy shape up to 4,000 bytes, saved split and trimmed to whole, the
  page model leaves 86 entries spare at the tightest
  (`test_the_first_save_after_an_update_fits`).

Those bounds hold for any arrangement of entries across the pages. The
native suite also runs NVS itself: `PagedNvs` (`test/test_config_records`)
models the pages, one-page strings, chunked blobs, the collection rule and
IDF's cleanup of a failed blob write, and every store shape (empty, the
largest whole, split at every escaped worst case, the standard variant's 18
full expressions) is saved in seeded order with system keys and
dispositions rewritten between saves and 29 padding entries beside them.
Every write must land and every save load back as saved.

An expression record holds at most 12 parameters (`kMaxExpressionParams`;
every type carries at most 11, guarded by `test_builtin_descriptors`) and
carries each key as its index in `kExpressionParamKeys`, so that table is
append-only, pinned by `test_builtin_descriptors`, and kept in byte-wise key
order (`test_param_wire_compat`); the JSON forms carry key names. A key no
expression reads is dropped at parse.

The caps that bound the stored JSON: 8 colours per surface segment (the app
offers 6), 4 segments per role (variants define at most 3), 16-byte segment
names, a 32-byte lamp name, a 64-byte password, a 32-byte SSID, a byte order
the strip driver knows, 8 home-mode type names (type ids only). The BLE
settings path and the web PUT both refuse an over-long password or SSID (BLE
logs the refusal, the web answers 400), and both ignore an unknown byte
order; `fromJson` also bounds what it loads (truncating a password or SSID
stored before the cap; the BOOT button clears a password), so no field can
make every save fail. A repeated (type, target) in the stored JSON loads
once, the first kept, as the records do.

A role's `rotate` (at most 49 B, `,"rotate":{"on":false,"periodS":120,"dir":"down"}`)
is stored only on a single-segment role and only when it differs from that
role's default, whatever the variant's `rotationSupported` says. A one-segment
role is at least 480 B shorter than the four-segment role the worst case
assumes, so the widest rest that carries `rotate` (one four-segment role, one
single-segment role with `rotate` at its widest) is 1316 B fully escaped,
with its NUL, under the 1746 B worst case, so the caps and NVS figures above
stand. `test_config_records` prints it as `[budget] rotating rest` and pins it
below the worst case.

Arduino's `initArduino` runs `nvs_flash_init` before `setup()`, erasing and
re-initializing on `NO_FREE_PAGES` or `NEW_VERSION_FOUND`.
`NvsConfigStore::initFlash` runs again first thing in `setup()` (a no-op on an
initialized partition) and erases only for those two codes or
`NOT_ENOUGH_SPACE`, the codes that mean the partition itself is unusable:
every stored setting is lost, but the lamp boots with Bluetooth, where leaving
it would need a USB reflash. Any other error (out of memory, a transient flash
fault) erases nothing and the lamp runs without NVS. A failed NimBLE init
skips every later BLE start, OTA radio pause and resume included.

A partition that initializes but is full gets space back on the first boot:
the WiFi driver's namespace is erased, and the legacy dispositions JSON (about
2.2 KB) is rewritten as the 701-byte blob, blob first, then erased. A legacy
JSON that does not parse, or parses to nothing while the blob holds entries,
is erased without replacing the blob.

Saves go through the `nvs_*` API, never `Preferences`: Preferences logs a
failed write through a formatting path that faulted on a starved heap. Every
save runs on the loop task: a web save is handed from the AsyncTCP task to
`webapp::tick` under a sequence number, and the handler takes only the result
for its own number. A handler that waits 3 s withdraws its body if the loop
has not taken it (500, nothing saved); if the loop has it, the handler
answers 202 and the save is attempted on its own; a failure is logged as
`[webapp] pending save failed`. A landed web save arms the reboot that loads
it, drops any pending retry, and makes `persistConfig` decline until that
reboot, so the running config cannot overwrite it; the commit drain waits
without retrying or logging, and a second web document is answered 409
rather than written over it (`config/save_gate.hpp`). The reboot waits while
OTA quiet mode is held, at most 10 minutes (`kRebootOtaWaitMaxMs`), after
which it logs `[webapp] reboot after 10 min OTA wait` and goes ahead. A
settings blob from the app that needs a reboot, whose save was deferred
(`[loop] settingsBlob: save deferred; reboot waits for it`), reboots once the
save lands and no firmware is being received; after 5 minutes with the save
still failing it gives up (`deferredReboot` in `config/save_gate.hpp`): no reboot,
the change runs until the next one, logged as `[loop] settingsBlob: save
never landed; no reboot`.

A reset at any point of a save leaves a whole config, and so does a save that
stops at a refused write. `test_config_records` cuts saves short at each of
their writes and loads what is left, then cuts the next save short at each
of its writes from every store that leaves. In memory that runs every shape
to every shape and on to every shape, each whole save also staged (407,600
loads); over the page model, the staged saves and
shape changes of the churn. Each section must load as the store before the
save had it or as saved.

- **Whole.** `cfg` is written (always with an `expressions` array, empty
  included, so deleting every expression and losing power before the erase
  cannot bring them back), then every record slot `x0`..`x31`, `xn`, `ko`,
  both rest keys and `cfgl` are erased. When NVS refuses the string
  (switching from split, it may have no page with room beside the split
  keys), it is staged: the whole JSON goes to a rest blob, `cfg` is erased,
  then the split keys and `cfgl`, then `cfg` is written and the rest keys
  erased.
- **Split.** The rest JSON is checked first (TooLarge writes nothing). The
  lamp section lives in the live rest blob when that is a staged whole,
  else in `cfg` when that holds the lamp section alone, else in `cfgl`. Unless
  `cfg` holds it, the save first stages the new lamp section in `cfgl` (a
  `cfgl` that is the current copy stays) and erases a stale lamp-only `cfg`.
  Then the records, `ko`, the count `xn`, every record slot past the count
  erased, and the rest blob, written to the rest key that is not live, erased
  first, before the live one is erased. A whole `cfg` is the authority until
  then and is erased only now. Last the lamp section goes to
  `cfg` and `cfgl` is erased. Sweeping every slot means a save cut short
  before its count lands leaves no record that a later save does not erase.
- **Load.** A `cfg` string with an `expressions` array (a whole save, or any
  older release's) is the authority and the load erases rest blobs and
  `cfgl` beside it. Otherwise the live rest blob holds the config: a staged
  whole one when it has an `expressions` array, else the rest beside the
  records, with the lamp section from `cfg`, or from `cfgl` while a save has
  `cfg` erased. A rest blob or `cfgl` that holds something unreadable sets
  `loadFailedWithData`, as an unparseable `cfg` does, so the first-boot save
  does not write defaults over it. On the device that is a blob over the
  read buffer, a chunk that fails its CRC (IDF erases that chunk on the read,
  and the blob on the next read or boot), or JSON that does not parse. A
  blob with a chunk missing is gone by the time it is read, so a lamp-only
  `cfg` with no rest blob beside it sets the flag too: a split store has
  lost its rest. The dispositions JSON key wins over the blob unless it is
  corrupt, or empty while the blob has entries.
- **A write that ran out of heap.** Two allocations inside a blob write can
  fail. The 12-byte `UsedPageNode` that records a chunk fails after the chunk
  is written: that chunk stays, uncleaned and named by no index, in the
  version the write was heading to. The 128-byte `HashListBlock` that
  `Page::writeItem` allocates before writing an entry fails before it: a
  chunk or the closing `BLOB_IDX` entry is not written, and the cleanup then
  erases the chunks recorded so far by position, as for any failed rewrite
  above (for the `BLOB_IDX`, every chunk). An orphan matters only if the key
  is written to its version again: that write doubles the orphan's position
  and IDF drops the whole blob at the next read or boot, while a boot with
  nothing written there erases the orphan. So every key whose blob write
  ran out of heap (`config/orphan_guard.hpp`, eight keys, then every key) is
  written until the next boot as an empty throwaway version, which takes
  the orphan's version, then the value, which lands in the other. The save
  that hit the failure fails and retries, every save after it lands, and
  the boot erases the orphans and loads the latest. A reset between the
  throwaway and the value drops that key at boot, as a cut-short write of it
  would. `NvsConfigStore` and the native suite's `PagedNvs` (whose erase of
  a replaced version takes one chunk per position, as `eraseMultiPageBlob`
  does) both write through `writeBlobPastOrphan`; `test_nvs_config_store`
  runs the device store itself over the page model, and
  `test_saves_after_an_out_of_memory_write_land_and_survive_the_boot` covers
  a record, the knockout blob and the rest.

Accepted risks. None stalls saves, and none touches the lamp section held in
`cfg`:

- A reset between a held key's throwaway and its value drops that key at
  boot. For a rest key that is every setting outside the lamp section and the
  expressions: they load as defaults, the load reports the store unreadable,
  and the first-boot save is held off so the defaults are not written over it.
  For a record, the knockout blob or `cfgl` it drops that record (logged at
  load), the knockout levels, or the lamp section staged there.
- The throwaway is a zero-length `nvs_set_blob`. `PagedNvs` accepts one; that
  IDF 5.5 does, and reads it back as an empty blob, wants a bench check.
- A record, `ko` or `disp` rewrite whose IDF cleanup takes the old copy (a
  failed rewrite, above) leaves that value missing until the save's retry
  writes it back from RAM; a reset before then loses it. Besides the two
  allocations, the cleanup runs when NVS cannot place a chunk or the index
  (`NOT_ENOUGH_SPACE`). The NVS budget above keeps that from happening at the
  caps; it holds only while the system keys (bonds, PHY calibration) stay
  inside the figures there.
- Everything the native suite proves about NVS is proved against `PagedNvs`,
  a model of IDF 5.5 (page placement, the 400-byte first-chunk rule,
  collection, cleanup by position, the boot's blob checks), never against IDF
  itself, so a divergence between the two goes unseen.

An edit rewrites one record and NVS skips the unchanged ones; a remove shifts
later records down a slot, so a reset mid-shift can leave one record in two
slots, and the load keeps the first of any repeated (type, target). The app's
edits are removes, edits and appends. A web save sends the whole document and
can reorder the list: saved whole that is one write and reset-safe, but saved
split a reset mid-save can leave an entry that moved in two slots and drop
the one it displaced. A web save that fails partway can leave records of the
web document beside the running config's rest, so the running config's save
goes pending and rewrites it whole (`SaveGate`). A record that fails to read
drops only itself, logged as `[cfg] expression record xN unreadable`. The
boot log's `[cfg] expressions=N from records|cfg json` is the loaded count;
the `cfg_bytes` on the line before is the `cfg` string alone.

**Downgrades.** Downgrades happen only on the bench, so this layout does not
try to serve one. An older release reads only the `cfg` string and the
legacy dispositions JSON key. A whole-saved lamp keeps everything there but
its dispositions. A split-saved lamp keeps only its lamp section (name,
password, brightness, setup flags); everything else reads as defaults, and
that release's own saves have to fit beside split keys it does not know.
Flashed forward again, whatever the older release saved is what loads; if
it saved nothing, the split store loads intact, dispositions included.

## What expressions cost at runtime

Measured with the xtensa compiler (`sizeof`) and the buffers each type keeps,
on the standard variant's 38-pixel shade; every heap block also pays about 12
bytes (TLSF header plus light poisoning):

| Type | Object | Buffers | Heap per instance |
|---|---|---|---|
| glitchy | 112 B | colours 32, saved pixels 152 | about 330 B |
| pulse | 140 B | colours 32 | about 200 B |
| breathing | 108 B | colours 32 | about 165 B |
| shifty | 148 B | colours 32, saved pixels 152, fill offsets 152 (directional fills only) | about 390-540 B |
| spotty | 112 B | colours 32, 5 spots 60 | about 240 B |
| flicker (shimmer) | 176 B | colours 32, a cell per zone pixel 304 | about 550 B |

A target-both expression is two instances, except a Pulse, which runs both
surfaces as one instance (`spansSurfaces`). Each configured expression adds
its `Config::expressions` slot (140 B, grown four slots at a time) and colour
list (about 44 B), and a 32-byte manager entry. The standard worst case (six
types at every target: 18 configs, 23 instances) is about 7.9 KB of instances,
2.7 KB of config, 0.8 KB of colours and 1 KB of entries: about 12.4 KB, against
about 18.4 KB before entries stopped copying parameters, the config vector
stopped doubling and shifty dropped its two per-pixel fade buffers. Up to four
transients come on top (a shifty pair is about 1 KB).

## What a crowd drives with no phone attached

A lamp in a crowd allocates on its own, with no app and no wisp. These paths
scale with the number of peers or the mesh's message rate, so each is bounded:

- **Cascade transients.** Every accepted MSG_COMMAND or MSG_EVENT invocation can
  start a one-shot heap Expression per target, alive until its animation
  completes (up to 180 s). `admitTransient`
  (`expressions/transient_admission.hpp`) admits them: at most 4 in flight,
  at most 2 of them a peer's (cascades and event echoes), so at least 2 slots
  are always free of peers for the lamp's own Test and greetings (a Test on
  both halves takes both), and only when the largest free block holds 2 KB
  per target plus the 4 KB headroom (8 KB for both halves, 6 KB for one), so
  a transient that starts can finish allocating. Under that, the lamp's own
  Test plays nothing here and logs `[expr] transient drop` (every build, at
  most every 5 s), but its cascade still goes out to the crowd
  (`cascadesFrom`), built from the invocation with no Expression made for
  it; a peer's invocation never re-sends. That type then waits a second
  (`LocalCooldown`) before the lamp tries it again, so a Test tapped over
  and over while the lamp is short neither retries nor resends each tap. The vector is reserved at
  `ExpressionManager::begin`, and the compositor reserves 16 behavior slots
  past its boot list for transients and live upserts.
- **Moments.** A Moment allocates only its transients, one at a time: a
  `flicker` burst (about 8 bytes a cell plus the Expression) for 700 ms,
  or a `pulse` for about 6 s, admitted as a peer's like any cascade. A
  Fireflies churns about 120 same-size pairs over 2.6 min, under 1 Hz. The
  director's own state is fixed-size members, with no per-peer storage, and
  its peer scans read the near view in place (`docs/dev/moments.md`).
- **Expression parameters live inline.** `ExpressionParams` holds one
  optional `uint32_t` per `kExpressionParamKeys` slot (88 bytes), so a config,
  an invocation, a transient and a manager entry carry their parameters with
  no heap node per key, and copying them allocates nothing. Each expression's
  config exists once, in `Config::expressions`; a manager entry (32 bytes)
  holds its Expression and type and reads the parameters from Config by
  (type, target). Only an entry with no Config entry behind it (a relayed op,
  an invocation) keeps its own copy.
- **Peer-supplied expression data.** An invocation keeps at most 8 colours and
  12 parameter keys; an expression upsert (app, or a relayed `expressionOp`)
  is rejected outside targets 1-3 and held to the same colour and parameter
  caps, and the stored list to 21 entries (`config/config_types.hpp`). An
  upsert of a type the variant does not register is rejected. A stored
  config over the caps is truncated on load with a `[cfg]` line and saved
  truncated on the next save; a stored out-of-range target is kept, since
  `remove` matches it exactly.
- **Frame authentication.** The COMMAND/EVENT HMAC runs on the WiFi task for
  every such frame overheard, before dedup. It hashes on stack-held SHA-256
  contexts and allocates nothing.
- **The BLE scan.** The scan callback hands the roster the six address bytes,
  so a lamp advertisement builds no 17-char string on the host task.
- **ESP-NOW frames in the driver.** Each queued frame holds a WiFi TX buffer
  until it airs. Past the driver's queue, `esp_now_send` fails with
  `ESP_ERR_ESPNOW_NO_MEM` (0x3067), and on the bench a burst of those was
  followed minutes later by the TX path wedging with its buffers (about 8 KB)
  held and every broadcast failing. `TxCredit`
  (`components/network/transport/tx_credit.hpp`) bounds the frames handed to
  the driver and not yet reported by its send callback to 6: every submit
  takes a credit, each send callback returns one, a refused submit returns it
  at once, and when credits are out and no callback has come back for 2 s
  they are presumed lost and the count resets. IDF documents no promise of
  one callback per accepted send (`esp_now.h`; the driver is closed), so a
  credit lost while other frames keep completing stays out until the link
  goes quiet for 2 s. A reset can be wrong: frames whose callbacks are only
  late may still sit in the driver, so after one the driver can hold up to
  2 x 6 frames until the next moment none is out. Timestamps compare by
  signed delta and never move
  backwards, since the WiFi task can stamp a callback just after the loop
  read `millis()`. A send past the bound fails before it reaches the driver
  and is counted. A reset logs `[espnow.tx] no tx callback, credits reset`
  on every build (at most every 10 s); the running counts show as `txstalls`
  (resets) and `txrefused` (sends dropped for want of a credit, a mesh
  resend among them) in `heap.get` and the `[heap]` trend line.
- **Staggered relays.** `MeshLink` holds the wisp-frame relays it delays
  (`RelayStagger`, `components/network/mesh/relay_stagger.hpp`) in fixed .bss
  tables, about 2.1 KB off free heap: 2 control-op slots of 608 bytes and 4
  WISP_HELLO / WISP_PALETTE slots of 224, sized for two wisps. Nothing
  allocates; a full table, or a known loop stall, relays inline. The loop
  sends a due relay only while a TX credit is free (`MeshLink::txReady`), so queued relays share the 6
  credits with the cascade drain and the mesh resends without hammering a
  stalled driver. A queued relay is refused, counted in `txrefused` and retried
  next pass only when another task takes the last credit between the check and
  the submit; an inline relay is refused like any other send.
- **Cascade fan-out.** A locally fired cascade (a cascading expression, or the
  app's Test) reaches every mesh peer, closest first. `CascadeScheduler`
  (`expressions/cascade_scheduler.hpp`) holds up to two fan-outs (peers and one
  serialized invocation each, about 2.2 KB of .bss: 50 MACs and a 768-byte
  payload per fan-out, above the 536-byte worst case). The loop sends up to 6
  frames per pass (`kBatchSize`, held to the 6 TX credits), only while credits
  remain (`TxCredit::available`, which applies the 2 s reset itself, so
  credits lost on a quiet link never hold the drain; a frame with none
  waits for a later pass), and passes are 100 ms
  apart, past a command's two resend copies
  40 ms apart, so a batch never overwrites the previous batch's resends in the
  10-slot ring. Each frame's `delayMs` is rewritten in place to keep the
  peer's intended fire time, (i + 1) x stagger capped at 1 s from the original
  fire, less the time the drain has taken, never below zero. Fifty peers take
  9 passes, so the last leaves at least 800 ms after the fire, and with a
  short stagger the far end of a 50-lamp crowd fires up to about a second
  late. A second cascade waits behind the one draining, its first batch a
  full gap after the last batch of the one before, so it never overwrites
  that batch's resends; a third replaces the one waiting. Fired together, the second starts its 9 passes after the
  first's, so its far end leaves about 1.7 s after its fire and fires that
  late. Those are floors on an idle link: relay traffic and mesh resends
  (each command's two copies 40 ms apart) take the same 6 credits, so a pass
  under contention sends fewer frames and the far end is later still, and a
  resend refused for want of a credit is dropped and counted in `txrefused`.
  An OTA start stops the drain, and a frame whose delay rewrite would
  overflow is dropped and logged.
- **Reads nobody asked for.** The section caches warm, and the state notify
  builds its payload, only while a BLE client holds the link; with no client
  the caches are freed every tick and rebuild on the next read, and the tick
  after a client connects warms them while the heap allows. The
  expressions section (about 4 KB at the worst case) is measured, then written
  by `JsonWriter` into one exactly-sized block; the smaller sections build
  their document only with the headroom free. A heap check cannot reserve
  the block, so the cache assign and the copy-out also catch `bad_alloc`
  (best effort, with no exception emergency pool).
- **OTA sessions.** The receiver's chunk bitmap (up to 8 KB per OFFER) is
  allocated only when `heapCanSpare` allows it; otherwise the OFFER is declined
  busy and the sender retries.

## The console lock

Lamp logs go through `Serial`, never stdout, so the first stdout write would be
an IDF driver's own log line, the WiFi and coex drivers logging from the
`esp_timer` task on core 0. The UART VFS driver creates its write lock (a
FreeRTOS mutex) on that first write, and newlib aborts when the allocation
fails, so on a starved heap a driver's error line crashed the lamp in
`lock_init_generic`. `setup()` writes one newline to stdout while the heap is
whole, which creates the lock at boot.

## Reading a crash record

Every build, released ones included, keeps a crash record (`core/crash_record`).
The loop samples the running minimum largest block, minimum free heap, roster
peak, and uptime into RTC memory at 1 Hz; the panic handler adds the reason, pc,
first four backtrace addresses, and core, and replaces the uptime with the
uptime at the fault. The handler is Arduino-ESP32's panic hook, which runs only
because the link wraps `esp_panic_handler` (`-Wl,--wrap=esp_panic_handler` in
`platformio.ini`); a build without the flag fails to link. RTC memory survives
a panic or watchdog reset but not power loss, so a magic value gates it. Until
the loop's first sample the snapshot is marked unsampled, since its heap and
roster figures are the ones taken at the re-arm early in setup. On the next
boot a panic, int/task/other watchdog, or brownout reset (or a captured panic
under any reset) bumps a counter and writes the record to NVS (namespace
`crash`, key `last`). A clean `esp_restart` (OTA, webapp reboot) and power-on
do not count.

Read it from the BLE `lamp` section:

```json
"resetReason": 4,
"lastCrash": {"count": 3, "reset": 4, "fw": 66055,
  "uptimeMs": 5412000, "minLargestBlock": 1300, "minFreeHeap": 5120,
  "rosterPeak": 12, "reason": "abort() was called", "core": 0,
  "pc": "0x4008a1b2", "bt": ["0x4008a1b2", "0x400d3c44", "0x400e0123",
  "0x400d5a10"]}
```

`reset` and `resetReason` are `esp_reset_reason_t` ordinals (4 panic, 5 int WDT,
6 task WDT, 7 other WDT, 9 brownout, 3 clean restart, 1 power-on). The heap and
roster fields are absent when RTC memory failed its check (a brownout often
does) or the boot died before its loop sampled; `core`/`pc`/`bt` are absent
unless the panic handler ran. `reason` is the CPU exception (`LoadProhibited`,
`IllegalInstruction`) or the abort's message: an assert or stack-overflow
message cut to 31 characters, or `abort() was called`, whose caller is the
fourth `bt` entry. A task-watchdog abort has no message, so its record carries
no `reason` and `reset` 6 names it. `uptimeMs` is the uptime at the fault when
the panic handler ran, else the last 1 Hz sample (0 when the boot died before
its loop sampled). It stops at 4294967295 (49.7 days) on a longer boot.
`minLargestBlock` near 1-2 KB with a high `rosterPeak` points at fragmentation.
Feed `pc` and `bt` to `xtensa-esp32-elf-addr2line -e firmware.elf` for the build
matching `fw` (packed `major<<16 | minor<<8 | patch`).

A stalled loop task records as `reset` 6 with `core` 1. The loop task is on the
ESP-IDF task watchdog from the end of `setup()` (`util/loop_watchdog.hpp`): it
feeds before every loop iteration, and between the steps of its longest blocking
work (each 64 KB block of the OTA upfront erase, the post-stream verify, the BLE
teardown for the web config). The timeout is the task watchdog's 5 s, widened to
30 s while a firmware or filesystem image is being received. The watchdog
interrupts whatever is running on core 1: for a loop that spins, `pc` and `bt`
point at the stall; for a loop blocked on a lock they point at the task that
ran instead (often the idle task), and only the serial log names `loopTask`.
After 3 task-watchdog resets in a row, each from a boot whose loop never
started an iteration 60 s into uptime, the next boot leaves the loop unwatched
and prints `[loopwd] 3 watchdog resets in a row` on every build, so a loop that
stalls on every boot freezes and stops resetting. The streak lives in RTC
memory, and any other reset (a power cycle, an update, a web save's reboot)
starts the watch again. A stall within 30 s of a firmware update's first boot
makes the bootloader roll back, and the record's `fw` is then the version that
booted after it. `setup()` runs unwatched: a hang there leaves the lamp dark
with no record until a power cycle.

## Reading the boot history

Every build, released ones included, keeps the last 5 boots in NVS
(`core/boot_history`, namespace `crash`, key `boots`) and prints the ended ones
to serial early in boot. Plugging a lamp into USB and opening `npm run
lamp:tap` resets it through the auto-reset circuit, so the history prints at
once. One line per boot, oldest first, `-1` the boot before this one:

```
[boots] -3 fw1.3.2 up 5h12m free 21480 big 1300 end panic
[boots] -2 fw1.3.2 up >1h00m free 30112 big 18200 end power
[boots] -1 fw1.3.2 up 42s free 40960 big 38000 end taskwdt
```

`up` is the uptime the boot reached, `free` its lowest free heap and `big` its
smallest largest free block, both in bytes and capped at 65535. `end` is the
reset that started the next boot: `power` (a power-on reset), `restart` (a
clean `esp_restart`: OTA, web reboot), `panic`, `intwdt`, `taskwdt`, `wdt`,
`brownout`, `ext`, `sleep`, `unknown`. The reset that USB plug-in and
`lamp:tap` cause reports as a power-on on the ESP32, so the boot they
interrupt reads `end power`.

The figures come from the crash record's RTC snapshot, sampled each second
from the first loop iteration on: when it survives the reset they are those
of the last sample, and after a panic or watchdog abort in a boot its loop
had sampled, `up` is the uptime at the fault. When it does not (a power cut,
often a brownout), the boot keeps the figures from the last milestone it
reached, 1 min, 1 h or 1 day, and `up` reads `>` that milestone.
`free - big -` means the boot died before any figure was taken: in `setup()`,
in its first loop iteration, or in its first minute after a power cut. Past
49.7 days the snapshot's uptime saturates and reads `>49d17h`. A boot writes
the history once at start and once per milestone, so at most four NVS writes
per boot.

## Why it recurs

The features that make the fleet feel alive constantly re-answer "who's near me,"
so the snapshot pattern gets reinvented per feature. It compiles clean, passes the
native tests, and only surfaces as a functional failure (the web config UI will not
serve, the wisp goes invisible) under real fleet and BLE load on the bench, never on
a quiet desk. Assume heap pressure, measure with the probe, and reach for
allocation-free access to shared fleet state.

Related: [`code-smells.md`](code-smells.md) (the reinvention smell) and
[`networking.md`](networking.md) (roster and mesh).
