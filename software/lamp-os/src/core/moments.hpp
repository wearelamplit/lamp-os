#pragma once

// Moments: rare crowd-level expressions driven by dispositions. The director
// owns triggers, pacing and the mesh hand-off and renders every pixel through
// an existing expression fired as a transient. Contract and tunables in
// docs/dev/moments.md.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "components/network/mesh/lamp_roster.hpp"
#include "config/config_types.hpp"
#include "core/moment_math.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_observer.hpp"
#include "expressions/transient_admission.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

// Everything the director reads from or does to the lamp. The firmware binds
// it to the live globals (lamp_moments.cpp); the native suite to fakes.
class MomentHost {
 public:
  virtual ~MomentHost() = default;
  // The near view crowd-dim reads (PersonalityEngine::nearbyPeers).
  virtual const std::vector<NearbyCopy>& nearbyPeers() = 0;
  virtual uint8_t disposition(const uint8_t mac[6]) = 0;
  virtual SocialMode socialMode() = 0;
  // Why no Moment may run right now (ota, wisp, preview, home), or null.
  virtual const char* suppressor() = 0;
  virtual bool greetingActive() = 0;
  // A frame may go out now: TX credit available and no OTA in progress.
  virtual bool canSend() = 0;
  virtual void send(const char* payload, size_t len) = 0;
  // ExpressionManager::triggerInvocation.
  virtual bool trigger(const ExpressionInvocation& inv, const uint8_t srcMac[6],
                       bool broadcast, TransientSource source) = 0;
  virtual uint16_t basePixels() = 0;
  // The surface's broadcast palette.
  virtual const std::vector<Color>& surfaceColors(bool base) = 0;
  // One `[moment]` line, under Print's 64 B stack buffer.
  virtual void log(const char* line) = 0;
};

class MomentDirector {
 public:
  enum class Role : uint8_t { None, Initiator, Joiner, Accepter };

  void begin(MomentHost* host, const uint8_t selfMac[6], uint32_t rngSeed, uint32_t nowMs);
  // Every loop: render deadlines, outbound frames and expired backoffs; the
  // crowd evaluation runs at 1 Hz inside it.
  void tick(uint32_t nowMs);
  // A received MSG_EVENT whose payload carries `mo`.
  void onFrame(const uint8_t srcMac[6], JsonObjectConst mo, uint32_t nowMs);

  // Starts `kind` now, past eligibility, conditions and the crowd cooldown,
  // never past a suppressor or a Moment already running. A zero seed rolls
  // one. True when the Moment started.
  bool force(MomentKind kind, uint32_t seed, uint32_t nowMs);
  void dumpState(uint32_t nowMs);

  bool active() const { return moment_.kind != 0; }
  MomentKind activeKind() const { return static_cast<MomentKind>(moment_.kind); }
  Role role() const { return moment_.role; }
  uint8_t growth(MomentKind kind) const { return kinds_[momentIndex(kind)].growth; }
  bool coolingDown(MomentKind kind, uint32_t nowMs) const;
  bool eligible(MomentKind kind, uint32_t nowMs) const;
  bool backoffArmed(MomentKind kind) const { return kinds_[momentIndex(kind)].backoffArmed; }

  static constexpr uint32_t kEvalPeriodMs = 1000;
  static constexpr uint32_t kStableLikedMs = 10 * kMinuteMs;
  static constexpr uint8_t kFirefliesMinLiked = 3;
  static constexpr uint32_t kResendMs = 200;
  static constexpr uint32_t kSendGiveUpMs = 1000;
  static constexpr uint32_t kWhisperHandOffMs = 3000;
  static constexpr size_t kDedupSlots = 8;

 private:
  struct KindState {
    uint8_t growth = 0;
    bool eligible = false;
    uint32_t nextEligibleMs = 0;
    bool cooling = false;
    uint32_t coolUntilMs = 0;
    bool backoffArmed = false;
    uint32_t backoffUntilMs = 0;
  };

  struct ActiveMoment {
    uint8_t kind = 0;  // MomentKind, 0 when idle
    Role role = Role::None;
    uint32_t seed = 0;
    uint32_t t0 = 0;
    uint32_t nextMs = 0;
    uint8_t origin[6] = {0};
    // Fireflies
    uint32_t periodMs = 0;
    uint16_t blink = 0;
    int32_t offset = 0;
    // Whisper: the frame's hop count and visited list, origin first.
    uint8_t hop = 0;
    uint8_t visited[kWhisperMaxHops][6] = {};
    // Whisper: the origin's colour, carried down the chain.
    bool hasChainColor = false;
    Color chainColor;
    // The colour this lamp renders in: the origin's for a Whisper hop.
    // Without one it renders its shade colours.
    bool hasTint = false;
    Color tint;
  };

  struct Outbox {
    bool pending = false;
    MomentFrame frame;
    uint32_t refMs = 0;
    uint8_t copiesLeft = 0;
    uint32_t dueMs = 0;
    uint32_t giveUpMs = 0;
    bool sentAny = false;
  };

  struct DedupEntry {
    uint8_t origin[6] = {0};
    uint32_t seed = 0;
    uint8_t kind = 0;
  };

  KindState& state(MomentKind k) { return kinds_[momentIndex(k)]; }
  const KindState& state(MomentKind k) const { return kinds_[momentIndex(k)]; }

  void evaluate(uint32_t nowMs);
  bool conditionsHold(MomentKind kind, uint32_t nowMs);
  bool mayInitiate(MomentKind kind, uint32_t nowMs);
  void initiate(MomentKind kind, uint32_t seed, uint32_t nowMs);
  void participate(MomentKind kind, uint32_t nowMs);
  void hearCrowd(MomentKind kind, uint32_t nowMs);
  void endMoment(const char* why);

  void onFireflies(const uint8_t src[6], const MomentFrame& f, uint32_t nowMs);
  void onWhisper(const uint8_t src[6], const MomentFrame& f, uint32_t nowMs);
  void startFireflies(uint32_t seed, uint32_t t0, const uint8_t origin[6], Role role,
                      uint32_t nowMs);
  void tickFireflies(uint32_t nowMs);
  void tickWhisper(uint32_t nowMs);

  void queueFrame(const MomentFrame& frame, uint32_t refMs, uint32_t nowMs);
  void tickOutbox(uint32_t nowMs);

  bool seen(const uint8_t origin[6], uint32_t seed, MomentKind kind) const;
  void remember(const uint8_t origin[6], uint32_t seed, MomentKind kind);

  // Appends `c` to the invocation, whitened when it sits too close to any of
  // this lamp's base stops to stand out on them.
  void addStandingOut(ExpressionInvocation& inv, Color c);
  // The tint when it carries light, else this lamp's shade colours that do,
  // else white.
  void addTintOrShade(ExpressionInvocation& inv);
  void renderBlink();
  void renderGlow();
  bool render(ExpressionInvocation& inv);
  void logf(const char* fmt, ...);

  MomentHost* host_ = nullptr;
  uint8_t self_[6] = {0};
  FastRng rng_{1};
  KindState kinds_[kMomentKindCount];
  uint32_t likedFingerprint_ = 0;
  uint32_t likedSinceMs_ = 0;
  uint8_t likedCount_ = 0;
  uint32_t lastEvalMs_ = 0;
  ActiveMoment moment_;
  Outbox outbox_;
  DedupEntry dedup_[kDedupSlots];
  uint8_t dedupHead_ = 0;
};

extern MomentDirector momentDirector;

// The MSG_EVENT receive path. A payload carrying `mo` goes to `director`
// (dropped when `mo` is not an object) and never reaches the expression
// mirror; any other invocation fans out to `observers`. Malformed payloads are
// dropped.
void deliverEvent(const uint8_t* payload, size_t len, const uint8_t srcMac[6], uint32_t nowMs,
                  MomentDirector& director, ExpressionObserverRegistry& observers);

}  // namespace lamp
