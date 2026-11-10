#include "core/moments.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <lampos/clock.hpp>

#include "components/network/protocol/event.hpp"
#include "expressions/bloom/bloom_expression.hpp"
#include "expressions/expression.hpp"
#include "expressions/primitives.hpp"

namespace lamp {

MomentDirector momentDirector;

namespace {

constexpr const char* kTwinkle = "flicker";
constexpr uint32_t kTwinkleStyle = 0;
constexpr uint32_t kFirefliesBurstMs = 700;

constexpr uint32_t kWhisperPulseSpeed = 3;
constexpr uint32_t kWhisperPulseSize = 6;
constexpr uint32_t kWhisperEasingSmooth = 1;
constexpr uint8_t kWhisperOpacity = 30;
constexpr size_t kBorrowedColors = 4;
// A colour this close to the base's own is whitened so it still stands out.
constexpr uint32_t kBorrowSameColor = 48;
constexpr uint8_t kBorrowWhiten = 102;

}  // namespace

void MomentDirector::begin(MomentHost* host, const uint8_t selfMac[6], uint32_t rngSeed,
                           uint32_t nowMs) {
  host_ = host;
  std::memcpy(self_, selfMac, 6);
  rng_ = FastRng(rngSeed);
  moment_ = ActiveMoment{};
  outbox_ = Outbox{};
  for (DedupEntry& e : dedup_) e = DedupEntry{};
  dedupHead_ = 0;
  const SocialMode mode = host_->socialMode();
  for (size_t i = 0; i < kMomentKindCount; ++i) {
    const MomentKind kind = static_cast<MomentKind>(i + 1);
    kinds_[i] = KindState{};
    kinds_[i].nextEligibleMs = nowMs + momentWaitMs(kind, 0, mode, rng_);
  }
  likedFingerprint_ = likedFingerprint(
      host_->nearbyPeers(), [this](const uint8_t* m) { return host_->disposition(m); },
      &likedCount_);
  likedSinceMs_ = nowMs;
  lastEvalMs_ = nowMs;
}

bool MomentDirector::coolingDown(MomentKind kind, uint32_t nowMs) const {
  const KindState& s = state(kind);
  return s.cooling && !timeReached(nowMs, s.coolUntilMs);
}

bool MomentDirector::eligible(MomentKind kind, uint32_t nowMs) const {
  const KindState& s = state(kind);
  return s.eligible || timeReached(nowMs, s.nextEligibleMs);
}

void MomentDirector::tick(uint32_t nowMs) {
  if (!host_) return;
  if (active() || outbox_.pending) {
    if (const char* why = host_->suppressor()) {
      outbox_.pending = false;
      endMoment(why);
    }
  }
  tickOutbox(nowMs);
  switch (moment_.kind) {
    case static_cast<uint8_t>(MomentKind::Fireflies): tickFireflies(nowMs); break;
    case static_cast<uint8_t>(MomentKind::Whisper):   tickWhisper(nowMs); break;
    default: break;
  }
  if (nowMs - lastEvalMs_ >= kEvalPeriodMs) {
    lastEvalMs_ = nowMs;
    evaluate(nowMs);
  }
  for (size_t i = 0; i < kMomentKindCount; ++i) {
    KindState& s = kinds_[i];
    if (!s.backoffArmed || !timeReached(nowMs, s.backoffUntilMs)) continue;
    s.backoffArmed = false;
    const MomentKind kind = static_cast<MomentKind>(i + 1);
    if (mayInitiate(kind, nowMs)) initiate(kind, 0, nowMs);
  }
}

void MomentDirector::evaluate(uint32_t nowMs) {
  const uint32_t fp = likedFingerprint(
      host_->nearbyPeers(), [this](const uint8_t* m) { return host_->disposition(m); },
      &likedCount_);
  if (fp != likedFingerprint_) {
    likedFingerprint_ = fp;
    likedSinceMs_ = nowMs;
  }
  likedSinceMs_ = lampos::retireStamp(nowMs, likedSinceMs_);
  // Latching both deadlines keeps a lamp idle past the 24-day signed-compare
  // horizon eligible and out of cooldown.
  for (KindState& s : kinds_) {
    if (!s.eligible && timeReached(nowMs, s.nextEligibleMs)) s.eligible = true;
    if (s.cooling && timeReached(nowMs, s.coolUntilMs)) s.cooling = false;
  }
  for (size_t i = 0; i < kMomentKindCount; ++i) {
    const MomentKind kind = static_cast<MomentKind>(i + 1);
    KindState& s = kinds_[i];
    const bool may = mayInitiate(kind, nowMs);
    if (may && !s.backoffArmed) {
      s.backoffArmed = true;
      s.backoffUntilMs = nowMs + momentBackoffMs(kind, rng_);
    } else if (!may) {
      s.backoffArmed = false;
    }
  }
}

bool MomentDirector::conditionsHold(MomentKind kind, uint32_t nowMs) {
  auto disposition = [this](const uint8_t* m) { return host_->disposition(m); };
  switch (kind) {
    case MomentKind::Fireflies:
      return likedCount_ >= kFirefliesMinLiked && nowMs - likedSinceMs_ >= kStableLikedMs;
    case MomentKind::Whisper:
      return hasWhisperTarget(host_->nearbyPeers(), nowMs, disposition);
  }
  return false;
}

bool MomentDirector::mayInitiate(MomentKind kind, uint32_t nowMs) {
  return eligible(kind, nowMs) && !coolingDown(kind, nowMs) && !active() &&
         host_->suppressor() == nullptr && !host_->greetingActive() &&
         conditionsHold(kind, nowMs);
}

void MomentDirector::participate(MomentKind kind, uint32_t nowMs) {
  KindState& s = state(kind);
  s.growth = momentGrow(kind, s.growth);
  s.nextEligibleMs = nowMs + momentWaitMs(kind, s.growth, host_->socialMode(), rng_);
  s.eligible = false;
  hearCrowd(kind, nowMs);
}

void MomentDirector::hearCrowd(MomentKind kind, uint32_t nowMs) {
  KindState& s = state(kind);
  const uint32_t cooldown = momentPacing(kind).crowdCooldownMs;
  if (cooldown == 0) return;
  const uint32_t until = nowMs + cooldown;
  if (!s.cooling || lampos::isLater(until, s.coolUntilMs)) s.coolUntilMs = until;
  s.cooling = true;
}

void MomentDirector::initiate(MomentKind kind, uint32_t seed, uint32_t nowMs) {
  if (seed == 0) seed = rng_.next();
  participate(kind, nowMs);
  logf("[moment] start k=%u s=%lu", static_cast<unsigned>(kind),
       static_cast<unsigned long>(seed));
  switch (kind) {
    case MomentKind::Fireflies: {
      startFireflies(seed, nowMs, self_, Role::Initiator, nowMs);
      remember(self_, seed, kind);
      MomentFrame f;
      f.kind = kind;
      f.seed = seed;
      queueFrame(f, nowMs, nowMs);
      break;
    }
    case MomentKind::Whisper:
      moment_ = ActiveMoment{};
      moment_.kind = static_cast<uint8_t>(kind);
      moment_.role = Role::Initiator;
      moment_.seed = seed;
      std::memcpy(moment_.origin, self_, 6);
      moment_.nextMs = nowMs;
      // The chain carries the origin's own colour; the origin itself glows in
      // its shade colours, since its base colour would not show on its base.
      if (!host_->surfaceColors(/*base=*/true).empty()) {
        moment_.hasChainColor = true;
        moment_.chainColor = host_->surfaceColors(/*base=*/true).front();
      }
      remember(self_, seed, kind);
      // The origin glows and announces the first hop at once, so every
      // bystander's crowd cooldown lands before a second origin's backoff.
      tickWhisper(nowMs);
      break;
  }
}

bool MomentDirector::force(MomentKind kind, uint32_t seed, uint32_t nowMs) {
  if (!host_) return false;
  if (active()) {
    logf("[moment] force k=%u skip busy", static_cast<unsigned>(kind));
    return false;
  }
  if (const char* why = host_->suppressor()) {
    logf("[moment] force k=%u skip %s", static_cast<unsigned>(kind), why);
    return false;
  }
  initiate(kind, seed, nowMs);
  return true;
}

void MomentDirector::endMoment(const char* why) {
  if (!active()) return;
  logf("[moment] end k=%u s=%lu %s", static_cast<unsigned>(moment_.kind),
       static_cast<unsigned long>(moment_.seed), why);
  moment_ = ActiveMoment{};
}

void MomentDirector::onFrame(const uint8_t srcMac[6], JsonObjectConst mo, uint32_t nowMs) {
  if (!host_) return;
  MomentFrame f;
  if (!decodeMomentFrame(mo, f)) return;
  hearCrowd(f.kind, nowMs);
  if (f.kind == MomentKind::Fireflies) {
    onFireflies(srcMac, f, nowMs);
  } else {
    onWhisper(srcMac, f, nowMs);
  }
}

void MomentDirector::onFireflies(const uint8_t src[6], const MomentFrame& f, uint32_t nowMs) {
  if (seen(src, f.seed, MomentKind::Fireflies)) return;
  if (active()) return;
  if (const char* why = host_->suppressor()) {
    logf("[moment] ff skip %s", why);
    return;
  }
  if (host_->disposition(src) < kDispositionNeutral) {
    logf("[moment] ff skip disposition");
    return;
  }
  startFireflies(f.seed, nowMs - f.ageMs, src, Role::Joiner, nowMs);
  if (!active()) return;
  remember(src, f.seed, MomentKind::Fireflies);
  participate(MomentKind::Fireflies, nowMs);
  logf("[moment] join k=1 s=%lu k#=%u", static_cast<unsigned long>(f.seed),
       static_cast<unsigned>(moment_.blink));
}

void MomentDirector::onWhisper(const uint8_t src[6], const MomentFrame& f, uint32_t nowMs) {
  if (std::memcmp(f.to, self_, 6) != 0) return;
  if (seen(f.visited[0], f.seed, MomentKind::Whisper)) {
    logf("[moment] wh skip duplicate");
    return;
  }
  if (active()) {
    logf("[moment] wh skip busy");
    return;
  }
  if (const char* why = host_->suppressor()) {
    logf("[moment] wh skip %s", why);
    return;
  }
  if (host_->disposition(src) < kDispositionNeutral) {
    logf("[moment] wh skip disposition");
    return;
  }
  remember(f.visited[0], f.seed, MomentKind::Whisper);
  moment_ = ActiveMoment{};
  moment_.kind = static_cast<uint8_t>(MomentKind::Whisper);
  moment_.role = Role::Accepter;
  moment_.seed = f.seed;
  std::memcpy(moment_.origin, f.visited[0], 6);
  moment_.hop = f.hop;
  std::memcpy(moment_.visited, f.visited, sizeof(moment_.visited));
  moment_.hasChainColor = f.hasColor;
  moment_.chainColor = f.color;
  moment_.hasTint = f.hasColor;
  moment_.tint = f.color;
  // The sender glowed `a` ago; this lamp glows one hand-off after it.
  moment_.nextMs = nowMs - f.ageMs + kWhisperHandOffMs;
  participate(MomentKind::Whisper, nowMs);
  logf("[moment] accept k=2 s=%lu h=%u", static_cast<unsigned long>(f.seed),
       static_cast<unsigned>(f.hop));
}

void MomentDirector::startFireflies(uint32_t seed, uint32_t t0, const uint8_t origin[6],
                                    Role role, uint32_t nowMs) {
  moment_ = ActiveMoment{};
  moment_.kind = static_cast<uint8_t>(MomentKind::Fireflies);
  moment_.role = role;
  moment_.seed = seed;
  moment_.t0 = t0;
  std::memcpy(moment_.origin, origin, 6);
  moment_.periodMs = firefliesPeriodMs(seed);
  moment_.blink = 1;
  moment_.offset = firefliesFirstOffset(seed, self_);
  // A late joiner starts at its first blink still ahead.
  while (moment_.blink <= kFirefliesBlinks &&
         lampos::isLater(nowMs, firefliesBlinkMs(t0, moment_.periodMs, moment_.blink, moment_.offset))) {
    moment_.offset = firefliesNextOffset(moment_.offset, moment_.blink);
    ++moment_.blink;
  }
  if (moment_.blink > kFirefliesBlinks) {
    moment_ = ActiveMoment{};
    return;
  }
  moment_.nextMs = firefliesBlinkMs(t0, moment_.periodMs, moment_.blink, moment_.offset);
}

void MomentDirector::tickFireflies(uint32_t nowMs) {
  if (!timeReached(nowMs, moment_.nextMs)) return;
  renderBlink();
#ifdef LAMP_DEBUG
  logf("[moment] ff k=%u d=%ld", static_cast<unsigned>(moment_.blink),
       static_cast<long>(moment_.offset));
#endif
  moment_.offset = firefliesNextOffset(moment_.offset, moment_.blink);
  ++moment_.blink;
  if (moment_.blink > kFirefliesBlinks) {
    endMoment("done");
    return;
  }
  moment_.nextMs = firefliesBlinkMs(moment_.t0, moment_.periodMs, moment_.blink, moment_.offset);
}

void MomentDirector::tickWhisper(uint32_t nowMs) {
  if (!timeReached(nowMs, moment_.nextMs)) return;
  renderGlow();
  if (moment_.hop >= kWhisperMaxHops) {
    endMoment("last hop");
    return;
  }
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = moment_.seed;
  f.hop = static_cast<uint8_t>(moment_.hop + 1);
  f.hasColor = moment_.hasChainColor;
  f.color = moment_.chainColor;
  std::memcpy(f.visited, moment_.visited, sizeof(f.visited));
  // Bounded apart from the hop rule above, so the list is never overrun.
  std::memcpy(f.visited[std::min<uint8_t>(moment_.hop, kWhisperMaxHops - 1)], self_, 6);
  auto disposition = [this](const uint8_t* m) { return host_->disposition(m); };
  if (selectWhisperHop(host_->nearbyPeers(), nowMs, self_, f.visited, f.hop, disposition,
                       f.to)) {
    queueFrame(f, nowMs, nowMs);
    logf("[moment] wh hop h=%u to=%02X%02X%02X", static_cast<unsigned>(f.hop), f.to[3],
         f.to[4], f.to[5]);
  } else {
    logf("[moment] wh chain ends, no hop");
  }
  endMoment("handed on");
}

void MomentDirector::queueFrame(const MomentFrame& frame, uint32_t refMs, uint32_t nowMs) {
  outbox_ = Outbox{};
  outbox_.pending = true;
  outbox_.frame = frame;
  outbox_.refMs = refMs;
  outbox_.copiesLeft = 2;
  outbox_.dueMs = nowMs;
  outbox_.giveUpMs = nowMs + kSendGiveUpMs;
  tickOutbox(nowMs);
}

void MomentDirector::tickOutbox(uint32_t nowMs) {
  if (!outbox_.pending || !timeReached(nowMs, outbox_.dueMs)) return;
  if (!host_->canSend()) {
    if (!timeReached(nowMs, outbox_.giveUpMs)) return;
    outbox_.pending = false;
    logf("[moment] send gave up k=%u", static_cast<unsigned>(outbox_.frame.kind));
    if (!outbox_.sentAny && outbox_.frame.kind == MomentKind::Fireflies &&
        moment_.kind == static_cast<uint8_t>(MomentKind::Fireflies) &&
        moment_.role == Role::Initiator && moment_.seed == outbox_.frame.seed) {
      endMoment("unsent");
    }
    return;
  }
  const uint32_t age = nowMs - outbox_.refMs;
  outbox_.frame.ageMs = static_cast<uint16_t>(age > kMomentMaxAgeMs ? kMomentMaxAgeMs : age);
  char payload[lamp_protocol::EVENT_MAX_PAYLOAD];
  const size_t len = encodeMomentFrame(outbox_.frame, payload, sizeof(payload));
  if (len > 0) {
    host_->send(payload, len);
    outbox_.sentAny = true;
  }
  if (--outbox_.copiesLeft == 0) {
    outbox_.pending = false;
    return;
  }
  outbox_.dueMs = nowMs + kResendMs;
  outbox_.giveUpMs = outbox_.dueMs + kSendGiveUpMs;
}

bool MomentDirector::seen(const uint8_t origin[6], uint32_t seed, MomentKind kind) const {
  for (const DedupEntry& e : dedup_) {
    if (e.kind == static_cast<uint8_t>(kind) && e.seed == seed &&
        std::memcmp(e.origin, origin, 6) == 0) {
      return true;
    }
  }
  return false;
}

void MomentDirector::remember(const uint8_t origin[6], uint32_t seed, MomentKind kind) {
  DedupEntry& e = dedup_[dedupHead_];
  std::memcpy(e.origin, origin, 6);
  e.seed = seed;
  e.kind = static_cast<uint8_t>(kind);
  dedupHead_ = static_cast<uint8_t>((dedupHead_ + 1) % kDedupSlots);
}

bool MomentDirector::render(ExpressionInvocation& inv) {
  const bool ok = host_->trigger(inv, self_, /*broadcast=*/false, TransientSource::Remote);
#ifdef LAMP_DEBUG
  if (!ok) logf("[moment] render refused %s", inv.type.c_str());
#endif
  return ok;
}

void MomentDirector::addStandingOut(ExpressionInvocation& inv, Color c) {
  const std::vector<Color>& base = host_->surfaceColors(/*base=*/true);
  auto nearestStop = [&base](Color x) {
    uint32_t nearest = UINT32_MAX;
    for (const Color& stop : base) nearest = std::min(nearest, colorDistance(x, stop));
    return nearest;
  };
  const uint32_t gap = nearestStop(c);
  if (gap <= kBorrowSameColor) {
    const Color whiter = bloomWhiten(c, kBorrowWhiten);
    // On a near-white base whitening only closes the gap, so the colour stays.
    if (nearestStop(whiter) > gap) c = whiter;
  }
  inv.colors.push_back(c);
}

void MomentDirector::addTintOrShade(ExpressionInvocation& inv) {
  if (moment_.hasTint && hasEnergy(moment_.tint)) {
    addStandingOut(inv, moment_.tint);
    return;
  }
  const std::vector<Color>& shade = host_->surfaceColors(/*base=*/false);
  for (size_t i = 0; i < shade.size() && i < kBorrowedColors; ++i) {
    if (hasEnergy(shade[i])) addStandingOut(inv, shade[i]);
  }
  if (inv.colors.empty()) addStandingOut(inv, Expression::kSafeFallbackColor);
}

void MomentDirector::renderBlink() {
  ExpressionInvocation inv;
  inv.type = kTwinkle;
  inv.target = TARGET_BASE;
  inv.parameters.set("fire", kTwinkleStyle);
  inv.parameters.set("count", firefliesStars(host_->basePixels()));
  inv.parameters.set("durationMax", kFirefliesBurstMs);
  inv.parameters.set("opacity", firefliesOpacity(moment_.blink));
  addTintOrShade(inv);
  render(inv);
}

void MomentDirector::renderGlow() {
  ExpressionInvocation inv;
  inv.type = "pulse";
  inv.target = TARGET_BASE;
  inv.parameters.set("loop", 0);
  inv.parameters.set("fullStrip", 1);
  inv.parameters.set("pulseSpeed", kWhisperPulseSpeed);
  inv.parameters.set("size", kWhisperPulseSize);
  inv.parameters.set("easing", kWhisperEasingSmooth);
  inv.parameters.set("opacity", kWhisperOpacity);
  addTintOrShade(inv);
  render(inv);
}

void MomentDirector::logf(const char* fmt, ...) {
  char line[64];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  host_->log(line);
}

void MomentDirector::dumpState(uint32_t nowMs) {
  if (!host_) return;
  for (size_t i = 0; i < kMomentKindCount; ++i) {
    const MomentKind kind = static_cast<MomentKind>(i + 1);
    const KindState& s = kinds_[i];
    const uint32_t waitS = eligible(kind, nowMs) ? 0 : (s.nextEligibleMs - nowMs) / 1000;
    const uint32_t coolS = coolingDown(kind, nowMs) ? (s.coolUntilMs - nowMs) / 1000 : 0;
    logf("[moment] k=%u g=%u wait=%lus cool=%lus bo=%d", static_cast<unsigned>(i + 1),
         static_cast<unsigned>(s.growth), static_cast<unsigned long>(waitS),
         static_cast<unsigned long>(coolS), s.backoffArmed ? 1 : 0);
  }
  logf("[moment] liked=%u stable=%lus", static_cast<unsigned>(likedCount_),
       static_cast<unsigned long>((nowMs - likedSinceMs_) / 1000));
  if (active()) {
    logf("[moment] active k=%u role=%u s=%lu", static_cast<unsigned>(moment_.kind),
         static_cast<unsigned>(moment_.role), static_cast<unsigned long>(moment_.seed));
  }
}

void deliverEvent(const uint8_t* payload, size_t len, const uint8_t srcMac[6], uint32_t nowMs,
                  MomentDirector& director, ExpressionObserverRegistry& observers) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!root["mo"].isNull()) {
    if (root["mo"].is<JsonObjectConst>()) {
      director.onFrame(srcMac, root["mo"].as<JsonObjectConst>(), nowMs);
    }
    return;
  }
  ExpressionInvocation inv;
  if (!parseInvocation(root, inv)) return;
  observers.fanOut(srcMac, inv);
}

}  // namespace lamp
