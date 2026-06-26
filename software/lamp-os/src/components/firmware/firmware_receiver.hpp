#pragma once

// Lamp-side OTA state machine. Receives MSG_FW_OFFER/CHUNK/DONE from a
// wisp (per the canonical wire format in lamp_protocol.hpp), validates,
// writes chunks to the inactive OTA partition via esp_ota_write_with_offset,
// verifies the ed25519 signature on the LSIG footer, and reboots into the
// new image.
//
// Cross-core safety:
//
//   - State machine runs on Core 1 (Arduino loop task). On OFFER: pick the
//     inactive OTA partition, then erase the ENTIRE image region
//     synchronously (64 KB block erases) BEFORE arming the
//     publishedOtaHandle_ gate or sending ACCEPT. The sender hasn't been
//     ACCEPTed, so nothing is in flight during the erase; and the gate is
//     disarmed, so Core 0 would drop any chunk anyway. Once the erase
//     succeeds, publish the partition pointer and arm the gate (release-
//     store). The first ACCEPT goes out synchronously; the remaining four in
//     the burst are queued for tick() to drain at 400 ms cadence so the loop
//     task isn't blocked freezing the renderer.
//   - Chunk writes run on Core 0 (WiFi recv task) via handleChunkOnRecvTask:
//     read the armed gate (acquire), then a pure esp_partition_write — NO
//     erase on the recv path. The region was erased upfront, so the
//     cache+IRQ-off erase stalls that used to overflow the ESP-NOW RX queue
//     mid-stream are gone; a <=200-byte write is sub-millisecond.
//   - eraseMux_ (named for history) now guards ONLY the receive bitmap_ RMW
//     in markChunkReceived against the const accessors (isBitmapFull /
//     firstMissingChunk). There is no erase on the recv path to serialize.
//   - On any abort path Core 1 disarms the gate (store 0, so Core 0 drops
//     chunks), then nulls publishedPartition_ and clears erasedForLen_. The
//     release-store on disarm pairs with Core 0's acquire-load.
//   - erasedForLen_ latch: set to the image length only after every block
//     erase returned ESP_OK. verifyAndApply checks it equals the offered
//     length before flipping the boot partition, so a re-OFFER at a different
//     length without re-erasing fails loud instead of booting a partial image.
//   - On DONE (Core 1): wait for the chunk bitmap to be fully set, disarm the
//     gate, then verifyAndApply (esp_partition_read + signature verify +
//     esp_ota_set_boot_partition).
//
// Channel mismatch is a SILENT DROP — no ACCEPT, no RESULT — per the locked
// scope decision. The lamp drops the offer at the handleControlOnLoop path
// before any side effects.

#include <atomic>
#include <cstdint>
#include <vector>

#include "../network/lamp_protocol.hpp"
#include "util/high_water.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#endif

namespace lamp {

class ShowReceiver;

// Transport abstraction for outbound MSG_FW_* responses (ACCEPT / REQ /
// RESULT). FirmwareReceiver doesn't know whether the OTA flow it's
// servicing came in over ESP-NOW (wisp → lamp mesh) or BLE (app → lamp
// direct); it just hands a serialized MSG_FW_* frame to its bound
// transport and asks for the lamp's own MAC for the sourceMac field.
//
// Production implementations:
//   - EspNowFirmwareTransport — wraps ShowReceiver::broadcastRaw, sends
//     to the mesh on channel LAMP_ESPNOW_CHANNEL. Used for the existing
//     wisp-driven OTA path.
//   - BleFirmwareTransport — notifies on CHAR_FW_STATUS to the BLE
//     connection that initiated the flow. Used for app-driven BLE OTA.
//
// Tests inject a MockFirmwareTransport that records calls for assertion.
class FirmwareTransport {
 public:
  virtual ~FirmwareTransport() = default;

  // Snapshot the lamp's own 6-byte MAC into out[6]. Called once during
  // FirmwareReceiver::begin() to populate the sourceMac field on every
  // outbound ACCEPT/REQ/RESULT.
  virtual void getMyMac(uint8_t out[6]) const = 0;

  // Send a pre-serialized MSG_FW_* frame. Returns true if the send was
  // accepted (queued / notified successfully).
  virtual bool sendFrame(const uint8_t* data, size_t len) = 0;
};

// Where did this control frame come from? Used by the FirmwareReceiver
// busy-check to enforce single-source-at-a-time semantics — a mesh OTA
// from the wisp can't be interrupted mid-flow by an app pushing over BLE
// (or vice versa), and an idempotent re-offer must come from the SAME
// transport + SAME source as the current flow to be accepted.
enum class FirmwareTransportKind : uint8_t {
  EspNow = 0,  // mesh — sourceMac identifies the wisp
  Ble    = 1,  // BLE  — bleConnHandle identifies the app's connection
};

// Slot payload that ShowReceiver's WiFi recv task posts (via
// postPendingFirmwareControl) and standard_lamp's Core 1 drain consumes.
// Discriminated by msgType (MSG_FW_OFFER / MSG_FW_DONE).
struct PendingFirmwareControl {
  uint8_t msgType;          // MSG_FW_OFFER or MSG_FW_DONE
  uint8_t sourceMac[6];
  uint8_t targetMac[6];
  uint16_t seq;
  // Wire-format version of the incoming frame (data[2]). The receiver
  // captures this on OFFER and replies (ACCEPT/REQ/RESULT) at the same
  // version — matching the peer's protocol so v0x04 distributors get
  // v0x04 responses from a v0x05-emitting receiver.
  uint8_t wireVersion = 0;
  // Transport context — populated by whichever dispatcher posted this
  // slot. Mesh path sets EspNow + bleConnHandle=0; BLE path sets Ble +
  // bleConnHandle=<the writer's NimBLE conn handle>.
  FirmwareTransportKind transportKind = FirmwareTransportKind::EspNow;
  uint16_t bleConnHandle = 0;
  // Union by msgType. Keeping struct flat (not actually a union) costs a
  // handful of bytes on the wire vs. an anonymous union, and trivially-
  // copyable types stay PendingTypedSlot-friendly.
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

// Forwarder defined in lamp.cpp; ShowReceiver's recv branches
// call this from the WiFi task to publish OFFER/DONE control frames.
void postPendingFirmwareControl(const PendingFirmwareControl& src);

class FirmwareReceiver {
 public:
  // Reciever lifecycle states. STREAMING is the only state in which
  // handleChunkOnRecvTask does anything; every other state drops chunks
  // silently. Transitions happen on Core 1 inside tick() or
  // handleControlOnLoop().
  enum class State : uint8_t {
    Idle             = 0,
    OfferReceived    = 1,  // ephemeral; transitions to Accepted same tick
    Accepted         = 2,  // esp_ota_begin OK; published otaHandle
    Streaming        = 3,
    Verify           = 4,  // signature check in progress
    Apply            = 5,  // boot partition set; awaiting reboot
    Failed           = 6,  // terminal error; reset to Idle on next tick
  };

  // Wire up. The receiver can service OTA flows over both ESP-NOW (wisp →
  // lamp via the mesh) and BLE (app → lamp direct) simultaneously, with
  // the single-source mutex enforcing only one active flow at a time.
  // `meshTransport` is required; `bleTransport` is optional (pass nullptr
  // if BLE OTA isn't wired in this build, e.g. a wisp using only the mesh
  // path). Caller retains ownership.
  //
  // Responses are routed per-call:
  //   - ACCEPT goes back to the OFFER's source transport (ctrl.transportKind)
  //   - REQ + RESULT during streaming go to the in-flight flow's transport
  //     (activeTransportKind_, captured at offer-accept time)
  void begin(FirmwareTransport* meshTransport,
             FirmwareTransport* bleTransport = nullptr);

  // Late-bind the BLE transport. The BLE GATT server is set up by
  // ble_control::start() AFTER firmwareReceiver.begin(), so its transport
  // adapter can't be passed at begin() time. ble_control::setFirmwareReceiver
  // calls this once the GATT chars are created. Caller retains ownership.
  void setBleTransport(FirmwareTransport* bleTransport) {
    bleTransport_ = bleTransport;
  }

  // Called from main loop on Core 1. Drains internal control queue,
  // runs stall/timeout watchdogs, generates MSG_FW_REQ on gaps. Cheap
  // when state == Idle.
  void tick(uint32_t nowMs);

  // Called from standard_lamp's Core 1 drain after pulling a
  // PendingFirmwareControl out of the pending slot. Dispatches by
  // msgType to the appropriate offer/done handler.
  void handleControlOnLoop(const PendingFirmwareControl& ctrl);

  // Called DIRECTLY from ShowReceiver's WiFi recv task (Core 0). Writes
  // the chunk payload to flash via esp_ota_write_with_offset and marks
  // the chunk-received bit in the bitmap. Bounded IO + bitmap set, no
  // heap, no JSON, no FreeRTOS blocking. Drops the chunk silently when
  // the receiver isn't in Streaming state (e.g. between Accepted and
  // first chunk after a state transition).
  void handleChunkOnRecvTask(const lamp_protocol::ParsedFwChunk& p);

  // Read-only state accessors (for tests + diagnostic logging).
  State state() const { return state_; }

  // True while an OTA session is mid-flow (anywhere between accepting
  // an OFFER and finishing verify/apply). Used by wifi.cpp to gate
  // periodic background scans so the radio stays pinned to
  // LAMP_ESPNOW_CHANNEL while ESP-NOW unicast in both directions is
  // load-bearing for ACCEPT/REQ/CHUNK/RESULT delivery.
  bool isInProgress() const {
    return state_ != State::Idle && state_ != State::Failed;
  }

  // Snapshot the active OTA peer's MAC into out[6]. Returns true when an
  // ESP-NOW OTA flow is in progress (so the OTA quiet-mode visual
  // indicator can look up the peer's base color from NearbyLamps).
  // Returns false for BLE OTA flows — the chunk source is the phone,
  // not a mesh peer; no lookup possible — and when isInProgress() is
  // false. Read-only; safe from the loop task.
  bool getPeerMac(uint8_t out[6]) const {
    if (!isInProgress()) return false;
    if (activeTransportKind_ != FirmwareTransportKind::EspNow) return false;
    for (int i = 0; i < 6; i++) out[i] = wispMac_[i];
    return true;
  }

  // Total chunks expected for the in-flight OFFER. Zero outside Streaming.
  uint16_t totalChunks() const { return offerTotalChunks_; }

  // Count of chunks received so far in the current OTA. Resets to 0 on
  // each new OFFER. Diagnostic counter — incremented by Core 0 on every
  // landed chunk; reads from Core 1 are coherent enough for a visual
  // progress bar (a one-frame stale read just lags one tick).
  // Monotonic progress for the OTA indicator — the receiver's analog of the
  // distributor's send-side high-water. Tracks UNIQUE chunks (uniqueChunks_),
  // not total writes: the dup-heavy recovery tail re-writes already-received
  // chunks, and feeding the bar raw writes races it past 100% (early
  // saturation + freeze/jump). uniqueChunks_ resets to 0 on every (re-)OFFER,
  // so a stalled session that aborts + re-offers the SAME image would collapse
  // the bar; the high-water only rises, and is reset only for a genuinely
  // different image (see onOfferOnLoop).
  uint32_t recvChunksCount() const { return recvProgress_.peek(); }

 private:
  // --- Helpers -----------------------------------------------------------

  // Offer handling — called from handleControlOnLoop on Core 1.
  void onOfferOnLoop(const PendingFirmwareControl& ctrl, uint32_t nowMs);
  void onDoneOnLoop(const PendingFirmwareControl& ctrl, uint32_t nowMs);

  // Bitmap helpers. The bitmap tracks one bit per expected chunk; sized
  // off the OFFER's totalChunks (or derived from totalLen/chunkSize).
  void resetBitmap(size_t totalChunks);
  void markChunkReceived(uint16_t chunkIdx);
  bool isBitmapFull() const;
  // Returns the first chunkIdx that is NOT received, or UINT16_MAX if
  // the bitmap is fully set. Linear scan; the bitmap is typically only
  // 1-2 KB so this is fast (~1.6 ms worst case at 8000 chunks).
  uint16_t firstMissingChunk() const;
  // Returns the length of the contiguous run of missing chunks starting
  // at firstMissing. Used to batch a single REQ for a whole run of
  // holes — single REQ for count=M consecutive misses instead of M
  // round trips at count=1. Capped at kMaxReqRunChunks so an early-
  // stream "everything missing" run doesn't dwarf forward progress.
  uint16_t firstMissingRunLen(uint16_t firstMissing) const;
  // Cap on the per-REQ run length. One REQ spans first..last-missing within
  // this many chunks (firstMissingRunLen), so scattered single-chunk drops
  // recover in one round trip instead of one-chunk-per-round.
  //
  // 20 = one flash sector. cap=1 does NOT converge under real ESP-NOW loss —
  // the receiver claws holes back one at a time and the session times out
  // before the bitmap fills (observed on flora). The old upper bound was the
  // JIT erase: a run spanning multiple sectors stalled Core 0 (cache+IRQs off)
  // long enough to overflow the RX queue mid-erase. Upfront-erase removes that
  // stall, so widening this is now safe and is the #18 recovery lever — it
  // stays at 20 (one sector) until that's stability-tested.
  static constexpr uint16_t kMaxReqRunChunks = 20;

  // Transmit helpers. All FW_* sends route through `transport_->sendFrame`.
  // For the ESP-NOW transport, that's `broadcastRaw` (wisp filters via
  // addressedToUs); for the BLE transport, it's a notify on the firmware-
  // status characteristic to the originating connection.
  bool sendAccept(const PendingFirmwareControl& ctrl,
                  lamp_protocol::FwAcceptStatus status);
  bool sendReq(uint16_t firstChunkIdx, uint16_t chunkCount,
               lamp_protocol::FwReqReason reason);
  bool sendResult(lamp_protocol::FwResultStatus status, uint8_t detail);

  // Abort path — clears published otaHandle, drains briefly, aborts the
  // OTA. Safe to call from Core 1 only.
  void abortOta();


  // Run the verify + apply sequence. Called on DONE arrival with full
  // bitmap. Reads the partition back into a buffer (via
  // esp_partition_read), parses LSIG footer, verifies signature, sets
  // boot partition, and triggers esp_restart. Returns the RESULT status
  // code (Success on full success, or the first failure encountered).
  lamp_protocol::FwResultStatus verifyAndApply();

  // Look up the FirmwareTransport instance bound to a given kind. Returns
  // nullptr if that transport wasn't wired at begin() time.
  FirmwareTransport* transportForKind(FirmwareTransportKind kind) const {
    if (kind == FirmwareTransportKind::Ble) return bleTransport_;
    return meshTransport_;
  }

  // --- State -------------------------------------------------------------

  FirmwareTransport* meshTransport_ = nullptr;
  FirmwareTransport* bleTransport_  = nullptr;

  State state_ = State::Idle;

  // Snapshot of the in-flight OFFER. Set on transition into Accepted;
  // referenced for the duration of Streaming + Verify.
  uint8_t  wispMac_[6] = {0};      // sourceMac of the OFFER (target of our ACCEPT/REQ/RESULT)
  uint8_t  myMac_[6]   = {0};      // our MAC (sourceMac of ACCEPT/REQ/RESULT)
  uint16_t offerSeq_   = 0;        // header seq from the OFFER (echoed in ACCEPT)
  // Active-flow transport context. Used by the onOfferOnLoop busy-check
  // to distinguish "same source re-offering" (idempotent ACCEPT) from
  // "different source trying to start a new OTA while one is in flight"
  // (DeclineBusy). Set at offer-accept time; cleared by abortOta / on
  // transition back to Idle.
  FirmwareTransportKind activeTransportKind_ = FirmwareTransportKind::EspNow;
  // Wire-format version of the incoming OFFER (data[2] of the frame).
  // Used as the wire version for every outbound ACCEPT/REQ/RESULT in
  // this session so we reply at the peer's protocol version.
  uint8_t activeWireVersion_ = lamp_protocol::PROTOCOL_VERSION_EMIT;
  uint16_t activeBleConnHandle_ = 0;
  uint32_t offerVersion_ = 0;
  uint32_t offerTotalLen_ = 0;
  uint16_t offerChunkSize_ = 0;
  uint16_t offerFooterLen_ = 0;
  uint16_t offerTotalChunks_ = 0;
  uint8_t  offerSha256Prefix_[lamp_protocol::FW_SHA256_PREFIX_LEN] = {0};
  char     offerChannel_[lamp_protocol::FW_CHANNEL_LEN + 1] = {0};

  // Sequence counters for our outbound FW frames (ACCEPT/REQ/RESULT).
  uint16_t fwOutSeq_ = 0;

  // Chunk-received bitmap. One bit per chunk; bit set means received.
  // Backed by std::vector<uint8_t> sized to ceil(totalChunks/8).
  std::vector<uint8_t> bitmap_;
  size_t bitmapTotalChunks_ = 0;

  // Timeouts:
  //   lastChunkMs_       — wall-clock of last successful chunk write
  //                        (drives stall-REQ; failed writes must trigger REQ)
  //   lastChunkSeenMs_   — wall-clock of last chunk that arrived at all,
  //                        irrespective of write outcome (drives the
  //                        no-progress abort; a stream where writes fail
  //                        is NOT a session-dead condition — chunks are
  //                        still flowing and the stall-REQ will recover)
  //   lastReqMs_         — wall-clock of last MSG_FW_REQ sent (rate-limit)
  //   streamingStartMs_  — wall-clock when we entered Streaming (60s budget)
  uint32_t lastChunkMs_     = 0;
  uint32_t lastChunkSeenMs_ = 0;
  uint32_t lastReqMs_       = 0;
  uint32_t streamingStartMs_ = 0;

  // Queued ACCEPT retries. The first ACCEPT is sent synchronously from
  // onOfferOnLoop; the rest are scheduled here for tick() to drain so
  // the loop task isn't blocked for ~1.6 s freezing the renderer
  // between sleeps. Quenched as soon as a chunk arrives — that's
  // proof the ACCEPT got through, no point burning more airtime.
  PendingFirmwareControl pendingAcceptCtrl_ = {};
  uint8_t                pendingAcceptCount_ = 0;
  uint32_t               nextAcceptMs_       = 0;

  // Diagnostic: Core 0 increments on every received chunk; Core 1's tick
  // periodically logs. Compares to the wisp's stream-progress log to
  // localise loss (RF vs. wisp-side throughput).
  uint32_t recvChunksCount_  = 0;
  uint32_t recvChunksLastLog_ = 0;
  // Unique chunks landed this image (0→1 bitmap transitions). Feeds the
  // progress high-water; bounded by totalChunks, unlike recvChunksCount_.
  uint32_t uniqueChunks_     = 0;

  // Monotonic progress for the indicator (see recvChunksCount()). Survives
  // session restarts; progressForLen_ tracks the image it was built for so a
  // genuinely different image resets it.
  HighWaterMark recvProgress_;
  uint32_t      progressForLen_ = 0;

  // Cross-core-published "armed" gate. Non-zero = streaming is armed, the
  // partition has been pre-erased, and Core 0's chunk handler may write
  // directly via esp_partition_write(publishedPartition_, ...). Zero =
  // no OTA in progress; Core 0 drops the chunk silently.
  //
  // Per the reconciliation doc's cross-core invariant section:
  //   - Core 1 writes via store(1, relaxed) after the manual pre-erase
  //     completes (which sets publishedPartition_ as well).
  //   - Core 0 reads via load(relaxed); 0 → drop the chunk silently.
  //   - Core 1 store(0, relaxed) BEFORE clearing publishedPartition_
  //     prevents a late Core 0 write from racing the teardown.
  std::atomic<uint32_t> publishedOtaHandle_{0};

  // Counter of in-flight esp_partition_write calls on Core 0. Incremented
  // by handleChunkOnRecvTask BEFORE the armed-gate check and decremented
  // AFTER the write (or on any early-return inside the function). Core 1's
  // verifyAndApply spins-with-yield until this drops to 0 before reading
  // back from the partition — guarantees no Core 0 write is still landing
  // bytes when verify starts the sha256 stream. Without this barrier,
  // exchange(0) on publishedOtaHandle_ is atomic but NOT synchronous:
  // Core 0 could have read armed=nonzero microseconds earlier, started
  // its esp_partition_write, and not yet completed when Core 1's
  // esp_partition_read begins — verify reads stale bytes, sigverify
  // FAILS even though every chunk was written successfully.
  std::atomic<int> writesInFlight_{0};

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Active OTA partition pointer, accessed by both Core 1 (set during
  // OFFER handling, cleared during abort/finalize) and Core 0 (read by
  // handleChunkOnRecvTask to target esp_partition_write). Aligned 32-bit
  // load/stores on Xtensa are atomic; the publishedOtaHandle_ gate above
  // provides the happens-before ordering — Core 0 never reads this
  // pointer unless publishedOtaHandle_ != 0, and Core 1 always zeroes
  // publishedOtaHandle_ before nulling this pointer.
  std::atomic<const esp_partition_t*> publishedPartition_{nullptr};

  // Guards the receive bitmap_ RMW (markChunkReceived) against the const
  // accessors (isBitmapFull / firstMissingChunk) — named eraseMux_ for
  // history; there is no erase on the recv path anymore (full-upfront erase).
  // mutable so const accessors (isBitmapFull / firstMissingChunk) can
  // take the spinlock around their reads. Synchronization is a logical
  // no-op WRT observed value — it just makes the read coherent against
  // Core 0's concurrent RMW.
  mutable portMUX_TYPE eraseMux_ = portMUX_INITIALIZER_UNLOCKED;

  // Erase-coverage latch: set to offerTotalLen_ after the full upfront
  // partition erase succeeds in onOfferOnLoop, checked in verifyAndApply,
  // cleared in abortOta. A mismatch at verify = a re-OFFER changed the image
  // length without re-erasing → fail loud, don't flip to a partial image.
  uint32_t erasedForLen_ = 0;
#endif

  // OFFER/DONE control intake uses standard_lamp's PendingTypedSlot<
  // PendingFirmwareControl> + postPendingFirmwareControl forwarder, in
  // line with the other Core 0 → Core 1 typed slots. The Core 1 drain in
  // lamp.cpp calls handleControlOnLoop() with the drained slot.

  // --- Inter-session quiet hold (mesh-only, multi-distributor case) ---
  // In a 3+ lamp mesh, a below-version receiver can be OFFERed by more
  // than one distributor in quick succession. If session A (from peer B)
  // times out / aborts, state goes Idle and without latching we'd
  // exitQuiet → compositor runs normal pipeline → IdleBehavior writes
  // defaultColors → strip flashes the lamp's base colour → then peer
  // C's OFFER arrives a moment later → re-enter quiet → indicator paints
  // again. That's the flash visible on receivers between back-to-back
  // OFFERs from different distributors. Defer exitQuiet by
  // kInterSessionQuietHoldMs so the gap is bridged invisibly when a
  // retry/handoff lands inside that window; if nothing arrives, the
  // hold expires and we resume normal animation.
  //
  // This is the receiver-side mirror of the proven distributor cooldown,
  // but minimal — we don't track a "last session" for the indicator's
  // hold render because the receive case is much shorter than a multi-
  // peer distribute wave (only the failure-then-retry gap matters).
  static constexpr uint32_t kInterSessionQuietHoldMs = 10000;
  bool     quietHeld_              = false;
  uint32_t quietHoldUntilMs_       = 0;
};

}  // namespace lamp
