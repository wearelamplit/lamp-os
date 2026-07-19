#pragma once

// Lamp-side gossip-OTA distributor: single-peer-at-a-time state machine that
// pushes this lamp's running firmware image to one nearby stale peer per
// session (Idle -> OfferSent -> Streaming -> Finalizing -> Done/Failed -> Idle).
// Targeting is event-driven: SocialBehavior::control calls considerPeerForOta
// for each nearby below-version peer; no periodic scan.
//
// Three contexts touch session state, all serialized by portMUX_TYPE stateMux_:
// the WiFi recv task (Core 0) parses ACCEPT/REQ/RESULT and pivots state; the
// Arduino loop (Core 1) runs considerPeerForOta + tick (timeouts, stall
// watchdog, tombstone reap); the streaming task runs OFFER retries + chunk
// emission. Mesh send + Serial.print happen OUTSIDE the mux to avoid pinning
// ISRs.

#include <cstddef>
#include <cstdint>

#include "components/network/protocol/fw_ota.hpp"  // FW_CHUNK_SIZE_BASELINE/_MAX

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

// FS-image OTA distributor behavior. nullptr (default) = firmware distribution;
// the FS distributor instance injects these. Native-safe (void* partition,
// function pointers).
struct FsDistributorHooks {
  // Source spiffs partition to stream (really const esp_partition_t*).
  const void* (*partition)();
  // Fixed image length (spiffs partition size) + FS manifest digest (8-byte
  // prefix + full 32) + the fw.lsig ed25519 signature over that digest, for the
  // OFFER auth trailer. Replaces the LSIG forward-scan + signed-region SHA (an FS
  // image has no footer, and a raw-partition SHA differs per lamp). false ->
  // Disabled.
  bool (*lengthAndDigest)(uint32_t* outLen, uint8_t outDigestPrefix[8],
                          uint8_t outFullDigest[32], uint8_t outSignature[64]);
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

  // Call once in setup() AFTER mesh_link is begin()'d so the transport's
  // MAC snapshot is valid. nullptr transport leaves the distributor Disabled.
  // Registers this instance with the shared streaming task; does not launch it.
  void begin(FirmwareTransport* transport);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Launch the one shared streaming task. Call once from setup() AFTER every
  // instance's begin() so the registry is fully published before the task reads
  // it.
  static void startSharedStreaming();
#endif

  // Inject FS-image OTA behavior (see FsDistributorHooks). Call BEFORE begin()
  // so begin() resolves the spiffs source instead of the running app partition.
  void setFsHooks(const FsDistributorHooks* hooks) { fsHooks_ = hooks; }

  // Drive the state machine. Called every loop() on Core 1; cheap when Idle.
  void tick(uint32_t nowMs);

  // Idempotent peer-trigger. Called from SocialBehavior::control on Core 1 per
  // nearby peer. No-op unless state == Idle, peer is below FIRMWARE_VERSION,
  // and the peer isn't in the backoff ring; otherwise transitions Idle ->
  // OfferSent, sends OFFER, kicks the streaming task. Additional gates (mesh
  // quiesce, receiver-busy) live in the wiring layer.
  //
  // peerProtocolVersion (peer's HELLO data[2]) is the wire version used for
  // every outbound OTA frame this session so old-protocol peers can parse it;
  // 0 = unknown HELLO, peer skipped. peerFwChannel is the peer's
  // {type}-{channel} from HELLO_TLV_FW_CHANNEL: known and != ours skips the
  // peer (no cross-variant/channel flash); unknown offers anyway.
  // peerMaxChunk is the peer's HELLO_TLV_FW_MAX_CHUNK (0 = peer doesn't
  // advertise): the session chunk size becomes min(FW_CHUNK_SIZE_MAX,
  // peerMaxChunk) when advertised, else FW_CHUNK_SIZE_BASELINE.
  // peerRssi is the peer's ESP-NOW HELLO RSSI (-127 = unknown, not gated):
  // skips the offer below kOtaMinRssiDbm rather than starting a doomed
  // direct-hop transfer.
  void considerPeerForOta(const uint8_t peerMac[6], uint32_t peerVersion,
                          uint8_t peerProtocolVersion, uint32_t nowMs,
                          const char* peerFwChannel = nullptr,
                          uint16_t peerMaxChunk = 0,
                          int8_t peerRssi = -127);

  // Inbound packet hooks: mesh_link dispatches MSG_FW_ACCEPT/REQ/RESULT
  // from its WiFi recv task. Idempotent on irrelevant packets (wrong target
  // MAC, state, session). Parse + state pivot + optional wake-semaphore give;
  // no mesh sends inside these.
  void onAcceptOnRecvTask(const lamp_protocol::ParsedFwAccept& a);
  void onReqOnRecvTask(const lamp_protocol::ParsedFwReq& r);
  void onResultOnRecvTask(const lamp_protocol::ParsedFwResult& r);

  // True while a session is mid-flow (OfferSent / Streaming / Finalizing).
  // Failed + Done are tombstones reaped to Idle on the next tick; reported as
  // NOT in-progress so a follow-up considerPeerForOta on a different peer
  // isn't rejected before the reaping tick runs.
  bool isInProgress() const;

  // Snapshot the active OTA target's MAC into out[6]. True while mid-flow. Used
  // by the OTA indicator to look up the receiver's base color from LampRoster.
  bool getPeerMac(uint8_t out[6]) const {
    if (!isInProgress()) return false;
    for (int i = 0; i < 6; i++) out[i] = targetMac_[i];
    return true;
  }

  // Chunks the streaming task has advanced past, for the OTA indicator. Returns
  // the high-water mark of nextChunkIdx_, not nextChunkIdx_ itself: a smart-REQ
  // rewinds nextChunkIdx_ to re-stream a hole, and feeding that to the bar
  // flickers it back toward 0%. High-water rises monotonically.
  uint32_t sentChunksCount() const {
    const uint32_t hw = sentProgress_.peek();
    return nextChunkIdx_ > hw ? nextChunkIdx_ : hw;
  }

  // Total chunks for the ACTIVE session at its negotiated chunk size (chunk
  // size varies per peer, so this isn't the image's baseline-chunk count).
  // Zero when idle.
  uint16_t totalChunks() const { return totalChunks_; }

  // Streaming and retry tunables.
  // Streaming cadence: push one chunk, then vTaskDelay to let the WiFi task
  // drain the TX queue. Below ~15ms the ESP-NOW RX queue overruns under RF
  // loss and burst density drives LED flicker. Sender-side only.
  static constexpr uint32_t kStreamingChunkSpacingMs   = 30;
  static constexpr uint32_t kStreamingQueueBackoffMs   = 5;
  // streamOneChunk stacks two max-size chunk buffers (~2.9 KB) below the
  // partition-read + esp_now_send call chain; size for headroom above them.
  static constexpr uint32_t kStreamingTaskStackSize    = 8192;
  // Above the Arduino loop (1), well below WiFi/IDF (18+).
  static constexpr uint32_t kStreamingTaskPriority     = 5;
  // Re-check state this often in case a wake give was lost.
  static constexpr uint32_t kStreamingIdlePollMs       = 250;
  static constexpr uint16_t kStreamProgressLogEvery    = 256;
  // OFFER retry. ESP-NOW unicast over a BLE-coex'd radio drops ~50% of initial
  // OFFER attempts at the MAC layer; resend reusing sessionOfferSeq_ so the
  // peer's firmwareDedup_ collapses dupes. 200ms recovers within the ACCEPT
  // window while letting the WiFi TX queue drain.
  static constexpr uint8_t  kMaxOfferRetries    = 8;
  static constexpr uint32_t kOfferRetryIntervalMs = 200;
  // Covers the receiver's full upfront partition erase before it ACCEPTs.
  static constexpr uint32_t kAcceptTimeoutMs    = 15000;
  static constexpr uint32_t kFinalizeTimeoutMs  = 30000;
  // DONE retry. Receiver's onDoneOnLoop is state-guarded so a duplicate DONE
  // post-verify is dropped. 300ms (vs OFFER's 200ms) because the peer is still
  // settling from seconds of partition flush; 4 x 300ms covers the ~50-200ms
  // verify+RESULT RTT with margin.
  static constexpr uint8_t  kMaxDoneRetries     = 4;
  static constexpr uint32_t kDoneRetryIntervalMs = 300;
  static constexpr uint32_t kChunkResendMs      = 1500;
  // Retries per chunk before failing the peer. 16 rides through ~24s of
  // BLE-busy windows (the control radio competes for ESP-NOW airtime).
  static constexpr uint8_t  kRetriesPerChunk    = 16;
  static constexpr uint32_t kPeerBackoffMs      = 600000;
  // Tail-end RESULT loss is recoverable on a fast retry; skip the 10-min penalty.
  static constexpr uint32_t kPeerFinalizeBackoffMs = 15000;
  // Per-session REQ budget. Far above any reasonable peer's amplification, so
  // the receiver's 10-min hard cap (not this) bounds a stuck session.
  static constexpr uint16_t kMaxReqPerSession   = 4096;

 private:
  void emitOffer(const uint8_t targetMac[6], uint32_t peerVersion, uint32_t nowMs);
  // Build + send OFFER frame from current session state (initial + retries).
  // Returns false on build/send failure.
  bool sendOfferFrame(const uint8_t targetMac[6], uint32_t nowMs, bool isRetry);
  void emitDone(uint32_t nowMs);

  void recordPeerFailure(uint32_t nowMs);
  void recordPeerFailureFinalize(uint32_t nowMs);
  // An offered peer replying already-current contradicts the below-version
  // read of it; that mismatch is the cross-variant tell (same-variant behind
  // peers reply Accept), so block it for the rest of the session instead of
  // the usual timed backoff.
  void recordPeerBlocklist(uint32_t nowMs);
  void resetSession();
  // Snapshot the just-completed peer + total for the indicator's inter-session
  // hold. Call on the Done transition under stateMux_ before resetSession()
  // blanks the fields; the write crosses to Core 0 (onResult recv task) while
  // the indicator's mux-less getLastSession read tolerates one torn frame.
  void captureLastSession();

  // Inbound dispatchers (Core 0 / WiFi recv task: caller takes the mux around
  // the state pivot; logging happens outside the mux).
  void onAccept(const lamp_protocol::ParsedFwAccept& a, uint32_t nowMs);
  void onReq(const lamp_protocol::ParsedFwReq& r, uint32_t nowMs);
  void onResult(const lamp_protocol::ParsedFwResult& r, uint32_t nowMs);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  bool readPartitionBytes(uint32_t offset, size_t len, uint8_t* buf) const;
  // First 8 bytes of SHA-256 over the signed region (totalLen - footer). Called
  // once from begin(); cached in sha256Prefix_.
  bool computeShaPrefixOnce(uint32_t totalLen);
#endif

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  static void streamingTaskTrampoline(void* arg);
  // Services every registered instance's step per wake; one shared task drives
  // all distributors.
  static void streamingTaskLoop();
  // One inner-loop iteration: send one OFFER retry if due, OR one chunk if
  // streaming. Returns false to put the task back on the wake semaphore.
  bool        streamingTaskStep(uint32_t nowMs);
  // Single-chunk emit. Returns 0 = sent, advance; 1 = NO_MEM, back off + retry
  // same chunk; 2 = partition read failure, session aborted in-place.
  int         streamOneChunk(uint32_t nowMs);
  // Wake the streaming task. Safe from recv task + tick().
  void        wakeStreamingTask();
  // Signed length of the running image into outLen. False if no valid footer.
  bool        discoverImageLength(uint32_t* outLen) const;
#endif

  // Peer backoff ring.
  static constexpr size_t kPenaltyRingSize = 8;
  struct PeerPenalty {
    uint8_t  mac[6];
    uint32_t backoffUntilMs;
    bool     used;
    // True = never expires (backoffUntilMs unused). Session-lifetime block,
    // not a timed backoff.
    bool     persistent;
  };
  bool peerIsInBackoff(const uint8_t mac[6], uint32_t nowMs) const;
  void notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                       uint32_t durationMs, bool persistent = false);

  FirmwareTransport* transport_ = nullptr;
  // nullptr = firmware distribution (default). Set on the FS distributor
  // instance by fs_ota::begin().
  const FsDistributorHooks* fsHooks_ = nullptr;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Resolved in begin(); used by readPartitionBytes for every chunk read.
  const esp_partition_t* runningPartition_ = nullptr;
#endif

  State    state_           = State::Disabled;
  uint32_t stateEnteredMs_  = 0;

  uint8_t  targetMac_[6]          = {0};
  // Wire version emitted for this target, captured from the peer's HELLO at
  // considerPeerForOta time. Reset to 0 in resetSession().
  uint8_t  targetProtocolVersion_ = 0;
  // Negotiated chunk size for this session, captured from the peer's HELLO
  // FW_MAX_CHUNK TLV at considerPeerForOta time. Reset to the baseline floor
  // in resetSession(). Drives totalChunks_, the per-chunk slice, and the
  // OFFER's chunkSize field; the CHUNK payload buffer stays sized to
  // FW_CHUNK_SIZE_MAX regardless (the ceiling every session fits under).
  uint16_t sessionChunkSize_      = lamp_protocol::FW_CHUNK_SIZE_BASELINE;
  uint16_t totalChunks_           = 0;
  uint16_t nextChunkIdx_           = 0;
  // Monotonic max of nextChunkIdx_, read by sentChunksCount() so the indicator
  // never regresses when nextChunkIdx_ rewinds to serve a smart-REQ.
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
  uint16_t lastBurstSentChunks_    = 0;
  // Bounded by kMaxReqPerSession; exceeding aborts the session with backoff.
  uint16_t reqCountThisSession_    = 0;
  // Smart-REQ resume cursor. On an early-gap REQ while far ahead, serve
  // [firstChunkIdx, reqEndIdx_) then jump nextChunkIdx_ back to resumeChunkIdx_
  // instead of re-streaming everything the receiver already has. 0 = no rewind.
  uint16_t resumeChunkIdx_         = 0;
  uint16_t reqEndIdx_              = 0;

  // First 8 bytes of SHA-256(signed region), computed once in begin() and
  // reused across every OFFER + DONE.
  uint8_t  sha256Prefix_[8]    = {0};
  bool     shaPrefixReady_     = false;
  // Full digest + LSIG-footer signature, carried in the OFFER auth trailer so a
  // receiver verifies authenticity before streaming. authReady_ is set only
  // after this lamp's own signature verifies against kFirmwarePubkey at begin();
  // an unverifiable running image never offers. FS distribution binds the same
  // fw.lsig signature over its manifest digest, so its OFFERs carry the trailer
  // too.
  uint8_t  sha256Full_[32]     = {0};
  uint8_t  imageSignature_[64] = {0};
  bool     authReady_          = false;
  uint32_t firmwareTotalLen_   = 0;
  uint16_t firmwareTotalChunks_ = 0;
  uint8_t  cachedSrcMac_[6]    = {0};

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Guards all session state shared between recv task (Core 0), loop task
  // (Core 1), and streaming task.
  portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;

  // One streaming task + wake sem shared across all instances. The fw/FS start
  // gates keep at most one registrant streaming at a time, so the shared loop
  // services whichever is active without interleaving real work. s_sharedWake
  // is a token-bucket of 1, given on transition into OfferSent/Streaming.
  // ponytail: fixed [2] holds the firmware + FS partition targets; bump if a
  // third OTA partition target appears.
  static FirmwareDistributor* s_streamers[2];
  static uint8_t              s_streamerCount;
  static SemaphoreHandle_t    s_sharedWake;
  static TaskHandle_t         s_sharedTask;
#endif

  PeerPenalty penalties_[kPenaltyRingSize] = {};

  // Inter-session quiet hold. During a fleet OTA wave several below-version
  // peers queue; without the latch each session's enterQuiet/exitQuiet leaves a
  // gap where the compositor flickers normal animation between indicators. Hold
  // quiet until kInterSessionQuietHoldMs of idle passes, and remember the last
  // peer so the indicator paints a held full bar through the gap.
  static constexpr uint32_t kInterSessionQuietHoldMs = 10000;
  bool     quietHeld_                   = false;
  // Set once the streaming task has entered quiet + torn down the radio for the
  // active session; reset in resetSession(). Radio teardown is deferred off the
  // recv task (enterQuiet's BLE-host/webserver shutdown isn't recv-task-safe) and
  // off OfferSent (the first ACCEPT should arrive under a live radio) to the
  // streaming task's first Streaming step. Guards against re-tearing on a
  // Finalizing→Streaming REQ bounce.
  bool     sessionQuietArmed_          = false;
  uint32_t quietHoldUntilMs_            = 0;
  uint8_t  lastSessionPeerMac_[6]       = {0};
  uint16_t lastSessionTotalChunks_      = 0;
  bool     lastSessionValid_            = false;

 public:
  // For ota_indicator's inter-session hold render. True + fills mac/total if a
  // recent session ended within the quiet-hold window.
  bool getLastSession(uint8_t outMac[6], uint16_t& outTotalChunks) const;
};

extern FirmwareDistributor firmwareDistributor;

}  // namespace lamp
