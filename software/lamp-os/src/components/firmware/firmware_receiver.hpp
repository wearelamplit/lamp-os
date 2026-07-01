#pragma once

// Lamp-side OTA state machine. Receives MSG_FW_OFFER/CHUNK/DONE, writes chunks
// to the inactive OTA partition, verifies the ed25519 LSIG-footer signature,
// and reboots into the new image.
//
// Cross-core barrier: Core 1 (loop) handles OFFER by erasing the entire image
// region synchronously, THEN publishing publishedPartition_ and arming the
// publishedOtaHandle_ gate (release-store). Core 0 (WiFi recv) writes chunks
// only while the gate reads non-zero (acquire) via a pure esp_partition_write,
// no erase on the recv path, so a write is sub-millisecond. Abort/teardown
// disarms the gate (store 0) before nulling the partition. erasedForLen_ holds
// the erased image length; verifyAndApply rejects a re-OFFER at a different
// length so a partial image never boots.
//
// Channel mismatch is a silent drop (no ACCEPT, no RESULT), at handleControlOnLoop.

#include <atomic>
#include <cstdint>
#include <vector>

#include "components/network/protocol/lamp_protocol.hpp"
#include "util/high_water.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#endif

namespace lamp {

class MeshLink;

// Transport abstraction for outbound MSG_FW_* responses (ACCEPT/REQ/RESULT).
// FirmwareReceiver hands a serialized frame to its bound transport without
// knowing whether the flow came in over ESP-NOW (mesh) or BLE (app direct).
// Production: EspNowFirmwareTransport (wraps MeshLink::broadcastRaw) and
// BleFirmwareTransport (notifies CHAR_FW_STATUS). Tests inject a mock.
class FirmwareTransport {
 public:
  virtual ~FirmwareTransport() = default;

  // Snapshot the lamp's own 6-byte MAC into out[6]. Called once during begin()
  // to populate sourceMac on every outbound ACCEPT/REQ/RESULT.
  virtual void getMyMac(uint8_t out[6]) const = 0;

  // Send a pre-serialized MSG_FW_* frame. Returns true if queued/notified.
  virtual bool sendFrame(const uint8_t* data, size_t len) = 0;
};

// Source of a control frame, for the single-source-at-a-time busy check: a mesh
// OTA can't be interrupted mid-flow by a BLE one (or vice versa), and an
// idempotent re-offer must come from the same transport + source.
enum class FirmwareTransportKind : uint8_t {
  EspNow = 0,  // mesh: sourceMac identifies the wisp
  Ble    = 1,  // BLE: bleConnHandle identifies the app's connection
};

// Slot posted by MeshLink's WiFi recv task (postPendingFirmwareControl) and
// consumed by Core 1's drain. Discriminated by msgType.
struct PendingFirmwareControl {
  uint8_t msgType;          // MSG_FW_OFFER or MSG_FW_DONE
  uint8_t sourceMac[6];
  uint8_t targetMac[6];
  uint16_t seq;
  // Wire version of the incoming frame (data[2]), captured on OFFER and used
  // for every reply so a peer gets responses at its own protocol version.
  uint8_t wireVersion = 0;
  // Set by the posting dispatcher: mesh path EspNow + bleConnHandle=0; BLE path
  // Ble + the writer's NimBLE conn handle.
  FirmwareTransportKind transportKind = FirmwareTransportKind::EspNow;
  uint16_t bleConnHandle = 0;
  // Flat (not a union) to stay trivially-copyable for PendingTypedSlot.
  struct {
    uint32_t version;
    uint32_t totalLen;
    uint16_t chunkSize;
    char     channel[lamp_protocol::FW_CHANNEL_LEN];
    uint8_t  sha256Prefix[lamp_protocol::FW_SHA256_PREFIX_LEN];
    uint16_t footerLen;
    uint16_t totalChunks;
  } offer;
  struct {
    uint32_t version;
    uint32_t totalLen;
    uint8_t  sha256Prefix[lamp_protocol::FW_SHA256_PREFIX_LEN];
    uint16_t footerLen;
  } done;
};

// Forwarder defined in lamp.cpp; MeshLink's recv branches
// call this from the WiFi task to publish OFFER/DONE control frames.
void postPendingFirmwareControl(const PendingFirmwareControl& src);

// FS-image OTA reuses this receiver's transfer machinery (upfront erase, the
// Core0/Core1 write barrier, chunk bitmap + gap-fill REQ) but targets the
// spiffs partition with a different verify/accept rule and MSG_FS_* frames.
// nullptr (default) = firmware OTA; the FS receiver instance injects these at
// begin(). Function pointers (not std::function) keep this heap-free and
// native-compilable; partition is void* since esp_partition_t is ESP-only.
struct FsReceiverHooks {
  // The spiffs partition to write into (really const esp_partition_t*).
  const void* (*partition)();
  // Accept a fresh offer? FS rule: offer.version == FIRMWARE_VERSION AND the
  // offered digest differs from our local FS digest. (Channel/chunkSize/busy
  // are checked by the receiver first.)
  bool (*shouldAccept)(uint32_t offerVersion, const uint8_t* offerDigestPrefix);
  // Verify the written partition (mount + manifest digest + ed25519 + version).
  // Returns Success or an FS failure code; no commit step.
  lamp_protocol::FwResultStatus (*verify)(const void* partition,
                                          uint32_t expectedVersion);
  // Post-verify apply for FS, in place of the firmware path's esp_restart():
  // the FS image is read-only at runtime, so remount SPIFFS + recompute the
  // local digest instead of rebooting. nullptr -> fall back to reboot.
  void (*finalize)();
  uint8_t acceptType;
  uint8_t reqType;
  uint8_t resultType;
};

class FirmwareReceiver {
 public:
  // STREAMING is the only state in which handleChunkOnRecvTask writes; every
  // other state drops chunks silently. Transitions on Core 1 (tick() /
  // handleControlOnLoop()).
  enum class State : uint8_t {
    Idle             = 0,
    Streaming        = 1,  // onOfferOnLoop erased + armed the gate; chunks flowing
    Verify           = 2,  // signature check in progress
    Apply            = 3,  // boot partition set; awaiting reboot
    Failed           = 4,  // terminal error; reset to Idle on next tick
  };

  // Services OTA over ESP-NOW (mesh) and BLE (app direct) simultaneously, with
  // the single-source mutex enforcing one active flow. meshTransport required;
  // bleTransport optional (nullptr when BLE OTA isn't wired). Caller retains
  // ownership. ACCEPT routes to the OFFER's source transport; REQ + RESULT
  // route to the in-flight flow's transport (captured at offer-accept time).
  void begin(FirmwareTransport* meshTransport,
             FirmwareTransport* bleTransport = nullptr);

  // Late-bind the BLE transport: the GATT server is set up by
  // ble_control::start() AFTER begin(), so it can't be passed at begin() time.
  void setBleTransport(FirmwareTransport* bleTransport) {
    bleTransport_ = bleTransport;
  }

  // Inject FS-image OTA behavior (see FsReceiverHooks). nullptr (default) =
  // firmware OTA; set only on the FS receiver instance by fs_ota::begin().
  void setFsHooks(const FsReceiverHooks* hooks) { fsHooks_ = hooks; }

  // Cross-OTA guard: true if the OTHER OTA path (FS vs firmware) is mid-flow;
  // they share the erase + quiet-mode machinery, so only one runs at a time.
  // nullptr (default) = no cross-check. Wired by fs_ota::begin().
  void setBusyGuard(bool (*fn)()) { busyGuard_ = fn; }

  // Periodic Core 1 pump: call every loop. Advances OTA timeouts and gap
  // recovery. Cheap when Idle.
  void tick(uint32_t nowMs);

  // Core 1 drain entry: dispatches a PendingFirmwareControl by msgType to the
  // offer/done handler.
  void handleControlOnLoop(const PendingFirmwareControl& ctrl);

  // Called DIRECTLY from the WiFi recv task (Core 0). Writes the chunk to flash
  // and marks its bitmap bit. Bounded IO + bitmap set, no heap/JSON/blocking.
  void handleChunkOnRecvTask(const lamp_protocol::ParsedFwChunk& p);

  State state() const { return state_; }

  // True while a session is mid-flow (OFFER accepted through verify/apply).
  // wifi.cpp gates background scans on this to keep the radio pinned to the
  // ESP-NOW channel for the duration.
  bool isInProgress() const {
    return state_ != State::Idle && state_ != State::Failed;
  }

  // Snapshot the active OTA peer's MAC into out[6]. True only for an in-flight
  // ESP-NOW flow (the indicator looks up the peer's base color); false for BLE
  // (chunk source is the phone) and when not in progress.
  bool getPeerMac(uint8_t out[6]) const {
    if (!isInProgress()) return false;
    if (activeTransportKind_ != FirmwareTransportKind::EspNow) return false;
    for (int i = 0; i < 6; i++) out[i] = wispMac_[i];
    return true;
  }

  // Total chunks expected for the in-flight OFFER. Zero outside Streaming.
  uint16_t totalChunks() const { return offerTotalChunks_; }

  // Monotonic progress for the OTA indicator (receiver-side high-water). Tracks
  // UNIQUE chunks, not total writes: the dup-heavy recovery tail re-writes
  // received chunks, and feeding the bar raw writes races it past 100%. Only
  // rises; reset only for a genuinely different image (see onOfferOnLoop).
  uint32_t recvChunksCount() const { return recvProgress_.peek(); }

 private:
  // Offer handling, called from handleControlOnLoop on Core 1.
  void onOfferOnLoop(const PendingFirmwareControl& ctrl, uint32_t nowMs);
  void onDoneOnLoop(const PendingFirmwareControl& ctrl, uint32_t nowMs);

  // One bit per expected chunk, sized off the OFFER's totalChunks.
  void resetBitmap(size_t totalChunks);
  void markChunkReceived(uint16_t chunkIdx);
  bool isBitmapFull() const;
  // First un-received chunkIdx, or UINT16_MAX if full. Linear scan over a
  // 1-2 KB bitmap (~1.6 ms worst case at 8000 chunks).
  uint16_t firstMissingChunk() const;
  // Length of the contiguous missing run at firstMissing, so one REQ batches a
  // whole run of holes instead of one round trip per chunk. Capped at
  // kMaxReqRunChunks.
  uint16_t firstMissingRunLen(uint16_t firstMissing) const;
  // Cap on the per-REQ run length so scattered drops recover in one round trip.
  // cap=1 does NOT converge under real ESP-NOW loss (session times out before
  // the bitmap fills). 20 = one flash sector; held here until a wider value is
  // stability-tested.
  static constexpr uint16_t kMaxReqRunChunks = 20;

  // All FW_* sends route through transport_->sendFrame (ESP-NOW broadcastRaw or
  // a BLE notify on the firmware-status characteristic).
  bool sendAccept(const PendingFirmwareControl& ctrl,
                  lamp_protocol::FwAcceptStatus status);
  bool sendReq(uint16_t firstChunkIdx, uint16_t chunkCount,
               lamp_protocol::FwReqReason reason);
  bool sendResult(lamp_protocol::FwResultStatus status, uint8_t detail);

  // Clears the published handle, drains briefly, aborts the OTA. Core 1 only.
  void abortOta();

  // Verify + apply, on DONE with a full bitmap: read the partition back, parse
  // the LSIG footer, verify the signature, set the boot partition, esp_restart.
  // Returns Success or the first failure encountered.
  lamp_protocol::FwResultStatus verifyAndApply();

  // Transport for a kind, or nullptr if it wasn't wired at begin().
  FirmwareTransport* transportForKind(FirmwareTransportKind kind) const {
    if (kind == FirmwareTransportKind::Ble) return bleTransport_;
    return meshTransport_;
  }

  FirmwareTransport* meshTransport_ = nullptr;
  FirmwareTransport* bleTransport_  = nullptr;

  // nullptr = firmware OTA (default). Set on the FS receiver by fs_ota::begin().
  const FsReceiverHooks* fsHooks_ = nullptr;

  // Cross-OTA busy guard (see setBusyGuard). nullptr = no cross-check.
  bool (*busyGuard_)() = nullptr;

  State state_ = State::Idle;

  // Snapshot of the in-flight OFFER.
  uint8_t  wispMac_[6] = {0};      // sourceMac of the OFFER (target of our ACCEPT/REQ/RESULT)
  uint8_t  myMac_[6]   = {0};      // our MAC (sourceMac of ACCEPT/REQ/RESULT)
  FirmwareTransportKind activeTransportKind_ = FirmwareTransportKind::EspNow;
  uint8_t activeWireVersion_ = lamp_protocol::PROTOCOL_VERSION_EMIT;
  uint16_t activeBleConnHandle_ = 0;
  uint32_t offerVersion_ = 0;
  uint32_t offerTotalLen_ = 0;
  uint16_t offerChunkSize_ = 0;
  uint16_t offerTotalChunks_ = 0;

  // Sequence counters for our outbound FW frames (ACCEPT/REQ/RESULT).
  uint16_t fwOutSeq_ = 0;

  // One bit per chunk (set = received), ceil(totalChunks/8).
  std::vector<uint8_t> bitmap_;
  size_t bitmapTotalChunks_ = 0;

  //   lastChunkMs_       last successful chunk write (drives stall-REQ)
  //   lastChunkSeenMs_   last chunk to arrive regardless of write outcome
  //                      (drives the no-progress abort; failed writes still
  //                      flow chunks and the stall-REQ recovers them)
  //   lastReqMs_         last MSG_FW_REQ sent (rate-limit)
  //   streamingStartMs_  entered Streaming (60s budget)
  uint32_t lastChunkMs_     = 0;
  uint32_t lastChunkSeenMs_ = 0;
  uint32_t lastReqMs_       = 0;
  uint32_t streamingStartMs_ = 0;

  // Queued ACCEPT retries: the first ACCEPT is synchronous, the rest drain in
  // tick() so the loop task isn't blocked ~1.6s. Quenched once a chunk arrives
  // (proof the ACCEPT got through).
  PendingFirmwareControl pendingAcceptCtrl_ = {};
  uint8_t                pendingAcceptCount_ = 0;
  uint32_t               nextAcceptMs_       = 0;

  // Diagnostic: Core 0 increments per chunk, Core 1 logs periodically; compared
  // to the sender's progress log to localise loss (RF vs sender throughput).
  uint32_t recvChunksCount_  = 0;
  uint32_t recvChunksLastLog_ = 0;
  // Unique chunks this image (0->1 bitmap transitions); feeds the high-water,
  // bounded by totalChunks unlike recvChunksCount_.
  uint32_t uniqueChunks_     = 0;

  HighWaterMark recvProgress_;       // monotonic indicator progress (see recvChunksCount())
  uint32_t      progressForLen_ = 0; // image length it was built for; reset when it differs

  // Cross-core armed gate. Non-zero = partition pre-erased and Core 0 may write
  // via esp_partition_write(publishedPartition_, ...); zero = drop the chunk.
  // Core 1 stores 1 after the pre-erase (and after setting publishedPartition_),
  // and stores 0 BEFORE clearing publishedPartition_ so a late Core 0 write
  // can't race teardown.
  std::atomic<uint32_t> publishedOtaHandle_{0};

  // In-flight Core 0 esp_partition_write count. Incremented before the
  // armed-gate check, decremented after the write (or any early return). Core 1
  // verifyAndApply spins-with-yield until it hits 0 before reading the partition
  // back: exchange(0) on the gate is atomic but not synchronous, so a write that
  // started microseconds earlier could still be landing bytes and make verify
  // read stale data and fail sigverify.
  std::atomic<int> writesInFlight_{0};

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Active OTA partition. Core 1 sets/clears it; Core 0 reads it to target
  // esp_partition_write. Aligned 32-bit access is atomic on Xtensa, and the
  // publishedOtaHandle_ gate orders it (Core 0 reads only when the gate is
  // non-zero; Core 1 zeroes the gate before nulling this).
  std::atomic<const esp_partition_t*> publishedPartition_{nullptr};

  // Guards the bitmap_ RMW (markChunkReceived) against the const accessors
  // (isBitmapFull / firstMissingChunk); mutable so they can take it around
  // their reads. There is no erase on the recv path (full upfront erase).
  mutable portMUX_TYPE eraseMux_ = portMUX_INITIALIZER_UNLOCKED;

  // Set to offerTotalLen_ after the upfront erase succeeds, checked in
  // verifyAndApply, cleared in abortOta. A mismatch means a re-OFFER changed
  // the image length without re-erasing: fail loud, don't flip a partial image.
  uint32_t erasedForLen_ = 0;
#endif

  // Inter-session quiet hold. In a 3+ lamp mesh a receiver can be OFFERed by
  // several distributors in quick succession; if one session aborts, exiting
  // quiet immediately flashes the base color before the next OFFER lands. Defer
  // exitQuiet by kInterSessionQuietHoldMs to bridge that gap. Receiver-side
  // mirror of the distributor cooldown, minus the last-session indicator hold.
  static constexpr uint32_t kInterSessionQuietHoldMs = 10000;
  bool     quietHeld_              = false;
  uint32_t quietHoldUntilMs_       = 0;
};

}  // namespace lamp
