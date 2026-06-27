#pragma once

// FirmwareDistributor — lamp-side gossip-OTA distributor. Single-peer-at-a-
// time state machine that pushes THIS lamp's running firmware image to one
// nearby peer per session.
//
// Mirrors the wisp's FirmwareDistributor (software/wisp/src/FirmwareDistributor.h)
// with the venue-specific adaptations spelled out below. The shared shape
// (state machine, peer backoff ring, per-chunk retries, OFFER retries,
// FINALIZE timeout, DONE retry, streaming task) is intentionally identical
// — those tunables were hardware-tuned for ESP-NOW under BLE coex and
// carry over unchanged. The
// reasons-of-thumb comments below explain WHY the lamp version differs.
//
// State machine (unchanged from wisp):
//
//   Disabled --(transport bound + begin ok)--> Idle
//   Idle      --(considerPeerForOta)-------->  OfferSent       (emitOffer)
//   OfferSent --(accept rx)----------------->  Streaming       (start chunk loop)
//   OfferSent --(timeout)------------------>   Failed          (backoff peer)
//   Streaming --(chunks done)-------------->   Finalizing      (emitDone)
//   Streaming --(REQ rx)-------------------->  Streaming       (rewind cursor to req)
//   Streaming --(stalls)------------------>    Failed          (chunk retries exhausted)
//   Finalizing --(result rx)-->                Done            (transition to Idle next tick)
//   Finalizing --(timeout)------>              Failed
//   Failed     --(next tick)----->             Idle            (peer in backoff ring)
//   Done       --(next tick)----->             Idle
//
// Adaptations from the wisp version (numbered to match the porting brief):
//
//   1. Byte source — running partition, not embedded blob.
//        Wisp reads from a FirmwareCarrier wrapping an embedded const
//        kEmbeddedFirmware[] array. The lamp reads from its currently-
//        running OTA partition via esp_partition_read on
//        esp_ota_get_running_partition(). The full signed image
//        (including the 96-byte LSIG footer) lives on that partition;
//        total length to distribute = runningPartition_->size, number
//        of chunks = ceil(totalLen / FW_CHUNK_SIZE).
//
//        The advertised version is lamp::FIRMWARE_VERSION (from
//        src/version.hpp). The channel is lamp::FIRMWARE_CHANNEL_STR.
//        The SHA-256 prefix is computed on begin() by streaming the
//        signed region (totalLen - 96 footer bytes) through mbedtls
//        SHA-256 and taking the first 8 bytes — same scheme as the
//        wisp's FirmwareCarrier prep.
//
//   2. Event-driven targeting (NOT scan-based).
//        Wisp scans LampInventory every 5 minutes for the lowest-version
//        peer. The lamp does NOT scan. Instead, SocialBehavior::control
//        already iterates nearby peers each tick; when the distributor is
//        Idle and a peer's firmwareVersion < ours, the social loop calls
//        considerPeerForOta(peerMac, peerVersion). This collapses the
//        scan-period decision down to "next nearby peer that's stale",
//        which is the gossip behaviour we want for the unconducted fleet
//        (no central scheduler — each lamp pushes to whichever stale peer
//        is currently in range).
//
//        Consequence: ALL the scan plumbing is gone from the lamp side —
//        no LampInventory*, no kInitialScanDelayMs/kScanPeriodMs, no
//        lastScanMs_/firstScan_, no pickTargetAndOffer(). The peer backoff
//        ring stays (a sourced-Failed peer must cool down before we try
//        again) and is consulted inside considerPeerForOta.
//
//   3. Mesh emit via FirmwareTransport interface.
//        Wisp uses MeshLink* directly. The lamp shares the receiver's
//        FirmwareTransport abstraction (defined in firmware_receiver.hpp)
//        so MSG_FW_OFFER/CHUNK/DONE go out through the same broadcastRaw
//        path the receiver uses for ACCEPT/REQ/RESULT. Production wiring
//        is EspNowFirmwareTransport in show_receiver.hpp.
//
//   4. Dual-core lamp vs unicore wisp.
//        Wisp is ESP32-C6 (unicore). Lamp is ESP32-WROOM (dual-core):
//        recv task on Core 0 (WiFi), main loop on Core 1 (Arduino). The
//        state mux discipline carries over verbatim — every shared-state
//        mutation goes through portMUX_TYPE stateMux_. The streaming
//        task is left unpinned (xTaskCreate, not xTaskCreatePinnedToCore)
//        so the scheduler can place it on whichever core has slack; mesh
//        send doesn't care which core calls it.
//
//   5. Single-source mutex coordination with receiver.
//        The lamp ALSO runs firmware_receiver (which RECEIVES OTA from
//        peers OR from the BLE app). Distributor + receiver share the
//        OTA partition (read vs. read+write — distributor only ever
//        reads from runningPartition_, receiver writes to the inactive
//        partition). For the spike, we keep the two state machines
//        INDEPENDENT and expose isInProgress() on each; the wiring in
//        lamp.cpp will gate considerPeerForOta on
//        firmwareReceiver.isInProgress() == false so we never try to
//        distribute while receiving. Future hardening can add a true
//        cross-state-machine mutex.
//
// Concurrency: three contexts touch session state (same as wisp, just on
// different cores).
//
//   - onAcceptOnRecvTask / onReqOnRecvTask / onResultOnRecvTask — called
//     from Core 0 (WiFi recv task) via show_receiver dispatch. Parses
//     ACCEPT/REQ/RESULT and pivots state under stateMux_. Never sends
//     mesh frames; never blocks.
//   - considerPeerForOta / tick — called from Core 1 (Arduino loop).
//     considerPeerForOta starts a session if eligible; tick drives the
//     ACCEPT timeout, the per-chunk stall watchdog, the FINALIZE timeout,
//     and the Failed/Done → Idle tombstone.
//   - The streaming task — its own FreeRTOS task created in begin().
//     Runs OFFER retries + chunk emission. tick() and the recv path
//     signal it via wakeSem_.
//
// Shared session state (state_, nextChunkIdx_, lastSentChunk_, lastSentMs_,
// currentChunkRetries_, offerRetryCount_, lastOfferSendMs_, targetMac_,
// sessionVersion_, etc.) is guarded by portMUX_TYPE stateMux_. The streaming
// task takes the mux only to snapshot/mutate small fields; mesh send +
// Serial.print happen OUTSIDE the mux to avoid pinning ISRs.

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_partition.h>

#include "util/high_water.hpp"
#endif

namespace lamp_protocol {
struct ParsedFwAccept;
struct ParsedFwReq;
struct ParsedFwResult;
}  // namespace lamp_protocol

namespace lamp {

class FirmwareTransport;

// FS-image OTA distributor behavior. Default (fsHooks_ == nullptr) = firmware
// distribution (path unchanged); the FS distributor instance injects these.
// Native-safe (void* partition, function pointers).
struct FsDistributorHooks {
  // Source partition to stream (really const esp_partition_t* — the spiffs
  // partition).
  const void* (*partition)();
  // Fixed image length (the spiffs partition size — mkspiffs pads the image to
  // fill it) + the FS manifest digest prefix advertised in OFFER. Replaces the
  // LSIG forward-scan + signed-region SHA (an FS image has no footer, and a
  // raw-partition SHA differs per lamp). Returns false → distributor Disabled.
  bool (*lengthAndDigest)(uint32_t* outLen, uint8_t outDigestPrefix[8]);
  uint8_t offerType;
  uint8_t chunkType;
  uint8_t doneType;
};

class FirmwareDistributor {
 public:
  enum class State : uint8_t {
    Disabled = 0,
    Idle,
    OfferSent,
    Streaming,
    Finalizing,
    Failed,
    Done,
  };

  // Wire up. Call once in setup() AFTER show_receiver is begin()'d so the
  // transport's MAC snapshot is valid. transport is required; nullptr
  // leaves the distributor Disabled. The running partition is resolved
  // here (esp_ota_get_running_partition) and the SHA-256 prefix of its
  // signed region is computed once and cached.
  void begin(FirmwareTransport* transport);

  // Inject FS-image OTA behavior (see FsDistributorHooks). Call BEFORE begin()
  // so begin() resolves the spiffs source instead of the running app
  // partition. nullptr (default) = firmware distribution, path unchanged.
  void setFsHooks(const FsDistributorHooks* hooks) { fsHooks_ = hooks; }

  // Drive the state machine. Called every loop() iteration on Core 1.
  // Cheap when state == Idle (early-out).
  void tick(uint32_t nowMs);

  // Idempotent peer-trigger hook. Called from SocialBehavior::control on
  // Core 1 after iterating each nearby peer.
  //
  //   - If state != Idle: no-op (an active session continues; a re-call
  //     with the in-flight peerMac is silently ignored).
  //   - If the peer is in our backoff ring: no-op.
  //   - If peerVersion >= our FIRMWARE_VERSION: no-op (peer isn't stale).
  //   - Otherwise: snapshot session state, transition Idle → OfferSent,
  //     send OFFER, and kick the streaming task to own retries from here.
  //
  // The wiring layer is responsible for additional gates (e.g. mesh
  // quiesce — don't start an OTA mid-cascade — and the receiver-busy
  // check — don't distribute while we're receiving).
  // peerProtocolVersion is the byte from data[2] of the peer's HELLO
  // (recorded in NearbyLamp::protocolVersion). The distributor uses it
  // for every outbound OTA frame in this session so old-protocol peers
  // can process our OFFER/CHUNK/DONE at their version. Zero means
  // "unknown" — we skip the peer (we haven't heard a HELLO yet).
  // peerFwChannel: the peer's `{type}-{channel}` from HELLO_TLV_FW_CHANNEL,
  // or nullptr/"" when unknown (older peer). When known and != ours, we skip
  // the OFFER (no point flashing a snafu lamp a standard image, or crossing
  // channels). Unknown → offer anyway; the receiver's silent-drop still gates.
  void considerPeerForOta(const uint8_t peerMac[6], uint32_t peerVersion,
                          uint8_t peerProtocolVersion, uint32_t nowMs,
                          const char* peerFwChannel = nullptr);

  // Inbound packet hooks — show_receiver dispatches MSG_FW_ACCEPT/REQ/
  // RESULT from its WiFi recv task. Idempotent on irrelevant packets
  // (wrong target MAC, wrong state, wrong session). Keep work tight: parse
  // + a state-machine pivot + an optional wake-semaphore give. No mesh
  // sends inside these.
  void onAcceptOnRecvTask(const lamp_protocol::ParsedFwAccept& a);
  void onReqOnRecvTask(const lamp_protocol::ParsedFwReq& r);
  void onResultOnRecvTask(const lamp_protocol::ParsedFwResult& r);

  // For mesh-quiesce gating + tests. True while a session is mid-flow
  // (OfferSent / Streaming / Finalizing). Failed + Done are tombstone
  // states that transition back to Idle on the next tick; we report them
  // as NOT in-progress so a follow-up considerPeerForOta on a different
  // peer doesn't get rejected just because we haven't ticked yet.
  bool isInProgress() const;

  // Snapshot the active OTA target's MAC into out[6]. Returns true when
  // a session is mid-flow (OfferSent / Streaming / Finalizing). Used by
  // the OTA quiet-mode visual indicator to look up the receiver's base
  // color from NearbyLamps for the progress overlay.
  bool getPeerMac(uint8_t out[6]) const {
    if (!isInProgress()) return false;
    for (int i = 0; i < 6; i++) out[i] = targetMac_[i];
    return true;
  }

  // Chunks the streaming task has advanced past for the current session.
  // Read by the OTA visual indicator. Returns the HIGH-WATER MARK of
  // nextChunkIdx_, not nextChunkIdx_ itself — when the receiver REQs
  // a hole in its bitmap (very common near the end of a session),
  // the smart-REQ path rewinds nextChunkIdx_ back to the requested
  // chunk to re-stream it. Using nextChunkIdx_ directly caused the
  // indicator's bar to flash from near-100% down to near-0% every
  // REQ rewind (the bar pixels flickered toward dim).
  // The high water mark tracks our forward progress monotonically so
  // the indicator shows the user the actual delivered position without
  // visual stutter from the REQ-rewind mechanic underneath.
  uint32_t sentChunksCount() const {
    const uint32_t hw = sentProgress_.peek();
    return nextChunkIdx_ > hw ? nextChunkIdx_ : hw;
  }

  // Total chunks for the running image, computed once at begin() from
  // the discovered image length. Stable for the distributor's lifetime
  // (the running partition doesn't change). Zero until begin() runs.
  uint16_t totalChunks() const { return firmwareTotalChunks_; }

  // --- Tunables (constexpr; tests static_assert against these). ---
  // Streaming task cadence. Push one chunk, then vTaskDelay between them to
  // let the WiFi task drain the TX queue. On NO_MEM from the send call we
  // delay kStreamingQueueBackoffMs and retry the same chunk. Runs at
  // kStreamingTaskPriority (above the Arduino loop, well below WiFi/IDF).
  //
  // Locked at 30ms (~33 chunks/sec). With the receiver erasing its whole
  // partition upfront, a chunk no longer triggers a mid-stream JIT erase, so
  // faster cadences (15-25ms) also complete on the bench. But below ~15ms the
  // ESP-NOW RX queue itself overruns under real RF loss (heavy recovery
  // thrash, no net speed gain), and faster cadences raise on-air burst
  // density — which tracks with visible LED flicker during OTA. 30ms keeps
  // the most RX-queue margin and runs flicker-free; the speed below it is
  // marginal and thinly sampled. Sender-side only — retune via OTA without
  // fleet lockstep.
  static constexpr uint32_t kStreamingChunkSpacingMs   = 30;
  static constexpr uint32_t kStreamingQueueBackoffMs   = 5;
  static constexpr uint32_t kStreamingTaskStackSize    = 4096;
  static constexpr uint32_t kStreamingTaskPriority     = 5;
  // Wake-semaphore poll timeout when the task has no work — task wakes
  // every N ms to re-check state in case a signal was lost (defensive).
  static constexpr uint32_t kStreamingIdlePollMs       = 250;
  // Log a stream-progress line every N chunks emitted.
  static constexpr uint16_t kStreamProgressLogEvery    = 256;
  // OFFER retry. ESP-NOW unicast over a BLE-coex'd radio drops packets
  // intermittently; the wisp's send-callback data showed ~50% of initial
  // OFFER attempts MAC-layer FAIL (hardware-confirmed). Resend
  // up to N times spaced kOfferRetryIntervalMs apart, reusing the same
  // sessionOfferSeq_ so the peer's firmwareDedup_ collapses dupes.
  //
  // 200ms between retries: short enough to recover a dropped OFFER within
  // the receiver's ACCEPT window, long enough to let the WiFi TX queue drain
  // between attempts.
  static constexpr uint8_t  kMaxOfferRetries    = 8;
  static constexpr uint32_t kOfferRetryIntervalMs = 200;
  static constexpr uint32_t kAcceptTimeoutMs    = 15000;  // covers the receiver's full upfront partition erase before it ACCEPTs
  static constexpr uint32_t kFinalizeTimeoutMs  = 30000;
  // DONE retry. The lamp-side receiver's onDoneOnLoop is naturally
  // idempotent (state-guarded on Streaming) so a duplicate DONE arriving
  // after verify+reboot is silently dropped. Mirror the wisp's pattern:
  // initial send + up to kMaxDoneRetries follow-ups, kDoneRetryIntervalMs
  // apart, reusing the same DONE seq across attempts.
  //
  // 300ms cadence (vs OFFER's 200ms): by the time we get here the peer
  // has just done seconds of partition flushing; its loop task is still
  // settling and a tighter cadence risks colliding with the RESULT emit.
  // 4 retries × 300ms = 1.2s window; peer RTT for verify+RESULT emit is
  // ~50-200ms under normal conditions, so a single delivered DONE
  // comfortably yields a RESULT before our budget exhausts.
  static constexpr uint8_t  kMaxDoneRetries     = 4;
  static constexpr uint32_t kDoneRetryIntervalMs = 300;
  static constexpr uint32_t kChunkResendMs      = 1500;
  // Retries per chunk before failing the peer. Originally 4 (sized for
  // wisp-as-sender where BLE coex isn't a factor). For lamp-as-sender
  // gossip OTA the BLE control radio competes for ESP-NOW airtime, so
  // chunk-send bursts get eaten in waves. Bumped to 16 to ride through
  // ~24 s of BLE-busy windows before failing the peer.
  static constexpr uint8_t  kRetriesPerChunk    = 16;
  // 10-minute peer cool-down after any session failure.
  static constexpr uint32_t kPeerBackoffMs      = 600000;
  // Short backoff for FINALIZE-timeout case: tail-end RESULT loss is
  // recoverable on a fast retry without the 10-minute penalty.
  static constexpr uint32_t kPeerFinalizeBackoffMs = 15000;
  // Per-session REQ budget. The dominant loss source under JIT-erase is
  // 3-chunk bursts every ~20 chunks (cache-disabled erase window blocks
  // Core 0 recv), and each REQ-served round drops a few of its own
  // chunks the same way — so the loss decays slowly rather than
  // converging in one pass. 4096 is far more than any reasonable hostile
  // peer would generate (their amplification factor against a single
  // OFFER is bounded), but ensures the receiver's 5-min hard cap (not
  // this budget) is what bounds a stuck session in practice. Future
  // optimization: pre-erase the partition at OFFER time so chunks flow
  // continuously and the REQ count stays in the single digits.
  static constexpr uint16_t kMaxReqPerSession   = 4096;

 private:
  void emitOffer(const uint8_t targetMac[6], uint32_t peerVersion, uint32_t nowMs);
  // Build + send OFFER frame using current session state. Used by emitOffer
  // for the initial send and by the streaming task's OfferSent retry path
  // for resends. Returns false on build/send failure.
  bool sendOfferFrame(const uint8_t targetMac[6], uint32_t nowMs, bool isRetry);
  // Build + send DONE frame using current session state. Called from the
  // streaming task after the last chunk emits.
  void emitDone(uint32_t nowMs);

  void recordPeerFailure(uint32_t nowMs);
  void recordPeerFailureFinalize(uint32_t nowMs);
  void resetSession();

  // Inbound dispatchers (run on Core 0 / WiFi recv task — caller takes the
  // mux around the state pivot; logging happens OUTSIDE the mux).
  void onAccept(const lamp_protocol::ParsedFwAccept& a, uint32_t nowMs);
  void onReq(const lamp_protocol::ParsedFwReq& r, uint32_t nowMs);
  void onResult(const lamp_protocol::ParsedFwResult& r, uint32_t nowMs);

  // --- Partition byte source --------------------------------------------
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Read len bytes from the running partition into buf. Returns true on
  // success. Used by streamOneChunk and (during begin) by the SHA-256
  // pre-compute pass.
  bool readPartitionBytes(uint32_t offset, size_t len, uint8_t* buf) const;
  // Compute the first 8 bytes of SHA-256(signed region) over the running
  // partition. signed-region length = totalLen - 96 footer bytes. Called
  // once from begin(); the result is cached in sha256Prefix_.
  bool computeShaPrefixOnce(uint32_t totalLen);
#endif

  // --- Streaming task ---
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  static void streamingTaskTrampoline(void* arg);
  void        streamingTaskLoop();
  // Called from the streaming task — returns true while there's still work
  // to do. Encapsulates one iteration of the inner loop: send one OFFER
  // retry if due, OR send one chunk if streaming, OR returns false to put
  // the task back on the wake semaphore.
  bool        streamingTaskStep(uint32_t nowMs);
  // Single-chunk emit. Returns:
  //   0 → chunk sent, advance to next.
  //   1 → ESP-NOW NO_MEM / send queue full; back off and retry same chunk.
  //   2 → partition read failure; session aborted in-place.
  int         streamOneChunk(uint32_t nowMs);
  // Signal the streaming task to wake. Safe from recv task + tick().
  void        wakeStreamingTask();
  // Forward-scan the running partition for the lowest valid LSIG footer and
  // return its signed length (the partition is larger than the image; sending
  // partition->size would stream erased flash garbage). Forward, not backward,
  // is load-bearing — see the .cpp body. Returns false on partition read
  // failure.
  bool        discoverImageLength(uint32_t* outLen) const;
#endif

  // Peer backoff ring.
  static constexpr size_t kPenaltyRingSize = 8;
  struct PeerPenalty {
    uint8_t  mac[6];
    uint32_t backoffUntilMs;
    bool     used;
  };
  bool peerIsInBackoff(const uint8_t mac[6], uint32_t nowMs) const;
  void notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                       uint32_t durationMs);

  // --- Wiring ---
  FirmwareTransport* transport_ = nullptr;
  // FS-image OTA hooks. nullptr = firmware distribution (default). Set only on
  // the FS distributor instance by fs_ota::begin().
  const FsDistributorHooks* fsHooks_ = nullptr;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Cached pointer to our running partition. Resolved in begin(); used by
  // readPartitionBytes for every chunk read. Lifetime-stable for the
  // duration of the running image.
  const esp_partition_t* runningPartition_ = nullptr;
#endif

  // --- State machine ---
  State    state_           = State::Disabled;
  uint32_t stateEnteredMs_  = 0;

  // --- Active session ---
  uint8_t  targetMac_[6]          = {0};
  // Wire-format version we emit for THIS target. Captured from the
  // peer's HELLO (see NearbyLamp::protocolVersion) at considerPeerForOta
  // time, then used for every outbound OFFER/CHUNK/DONE in this
  // session. Reset to 0 in resetSession().
  uint8_t  targetProtocolVersion_ = 0;
  uint16_t totalChunks_           = 0;
  uint16_t nextChunkIdx_           = 0;
  // Monotonic max of nextChunkIdx_ for the current session — read by
  // sentChunksCount() so the indicator bar never regresses when
  // nextChunkIdx_ rewinds to serve a smart-REQ. observe()'d wherever
  // nextChunkIdx_ moves forward; reset() in resetSession().
  HighWaterMark sentProgress_;
  uint16_t lastSentChunk_          = 0;
  uint32_t lastSentMs_             = 0;
  uint8_t  currentChunkRetries_    = 0;
  uint16_t sessionOfferSeq_        = 0;
  uint32_t sessionVersion_         = 0;
  uint32_t sessionTotalLen_        = 0;
  uint16_t seqCounter_             = 0;
  uint32_t lastOfferSendMs_        = 0;
  uint8_t  offerRetryCount_        = 0;
  // Rolling counter of chunks emitted since the last progress-log line.
  uint16_t lastBurstSentChunks_    = 0;
  // Per-session REQ counter. Bounded by kMaxReqPerSession; exceeding
  // aborts the session with peer backoff.
  uint16_t reqCountThisSession_    = 0;
  // Smart-REQ resume cursor. When the receiver REQs an early missing
  // chunk while the sender is far ahead in forward progress, naive
  // "rewind nextChunkIdx_ to first missing" re-streams everything from
  // the gap to the current position — most of which the receiver
  // already has in its bitmap (wasted RF + BLE coex pressure). Instead,
  // we save the forward-progress position into `resumeChunkIdx_`, serve
  // the requested chunks at `[firstChunkIdx, reqEndIdx_)`, then jump
  // back to `resumeChunkIdx_` to keep moving forward. 0 = no rewind
  // in progress.
  uint16_t resumeChunkIdx_         = 0;
  uint16_t reqEndIdx_              = 0;

  // --- Cached identity ---
  // First 8 bytes of SHA-256(signed region). Computed once in begin() from
  // the running partition; reused across every OFFER + DONE emit.
  uint8_t  sha256Prefix_[8]    = {0};
  bool     shaPrefixReady_     = false;
  uint32_t firmwareTotalLen_   = 0;
  uint16_t firmwareTotalChunks_ = 0;
  // Cached MAC of this lamp for chunk-frame source address. Snapshotted
  // from transport_->getMyMac() in begin().
  uint8_t  cachedSrcMac_[6]    = {0};

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Critical-section mux protecting all session state shared between the
  // recv task (Core 0), the loop task (Core 1), and the streaming task.
  portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
  // Wake semaphore for the streaming task. Given by the entity that
  // transitions state into OfferSent or Streaming (initial offer, ACCEPT
  // rx, REQ rewind). Token-bucket of 1 — multiple gives collapse to a
  // single wake.
  SemaphoreHandle_t wakeSem_       = nullptr;
  TaskHandle_t      streamingTask_ = nullptr;
#endif

  // --- Backoff bookkeeping ---
  PeerPenalty penalties_[kPenaltyRingSize] = {};
  size_t      penaltyHead_                 = 0;

  // --- Inter-session quiet hold ---
  // The distributor handles one peer at a time but during a fleet OTA
  // wave there are usually several below-version peers queued. Without
  // this latch, each session pair (enterQuiet on emitOffer, exitQuiet
  // on Done) leaves a window where the compositor runs the normal
  // behavior pipeline and the strip momentarily shows fade/wisp paint
  // between progress indicators — observed as a flicker on the
  // distributor lamp. Hold quiet through the gap: keep the refcount up
  // until kInterSessionQuietHoldMs of true idle has passed with no new
  // session, and remember the last completed peer so the indicator can
  // paint a held "we just finished sending to X" full bar through the
  // gap rather than a flicker of normal animation.
  static constexpr uint32_t kInterSessionQuietHoldMs = 10000;
  bool     quietHeld_                   = false;
  uint32_t quietHoldUntilMs_            = 0;
  // Last completed session's identity — used by ota_indicator to keep
  // painting the FROM-us TO-them bar at 100% during the held gap.
  uint8_t  lastSessionPeerMac_[6]       = {0};
  uint16_t lastSessionTotalChunks_      = 0;
  bool     lastSessionValid_            = false;

 public:
  // For ota_indicator's inter-session hold render. Returns true and
  // fills mac+total if a recent session ended within the quiet-hold
  // window. False once the hold expires or we never sent.
  bool getLastSession(uint8_t outMac[6], uint16_t& outTotalChunks) const;
};

// Single global instance, defined in lamp.cpp. SocialBehavior +
// other behaviors call into this directly (matches the pattern used by
// `nearbyLamps`, `personalityEngine`, etc.).
extern FirmwareDistributor firmwareDistributor;

}  // namespace lamp
