// Native pin for Moments (docs/dev/moments.md): the pure math, the director
// against a fake host, and multi-lamp simulations over a fake mesh bus.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/moment_math.cpp"
#include "core/moments.cpp"
#include "expressions/expression_invocation.cpp"
#include "expressions/expression_observer.cpp"
#include "util/color.cpp"

using namespace lamp;

namespace {

constexpr uint32_t kMin = 60000;

void macOf(uint8_t n, uint8_t out[6]) {
  const uint8_t m[6] = {0x24, 0x0A, 0xC4, 0x10, 0x20, n};
  std::memcpy(out, m, 6);
}

uint64_t macKey(const uint8_t m[6]) {
  uint64_t k = 0;
  for (int i = 0; i < 6; ++i) k = (k << 8) | m[i];
  return k;
}

NearbyCopy peerOf(uint8_t n, int8_t rssi, uint32_t seenMs) {
  NearbyCopy p;
  std::snprintf(p.name, sizeof(p.name), "lamp%u", static_cast<unsigned>(n));
  macOf(n, p.mac);
  p.hasMac = true;
  p.lastRssi = rssi;
  p.lastSeenNearMs = seenMs;
  p.lastSeenBleMs = seenMs;
  return p;
}

// A BLE sighting at `ms`: renews presence and the RSSI's freshness together.
void seeAt(NearbyCopy& p, uint32_t ms) {
  p.lastSeenNearMs = ms;
  p.lastSeenBleMs = ms;
}

struct Render {
  ExpressionInvocation inv;
  uint8_t src[6];
  bool broadcast;
  TransientSource source;
  uint32_t atMs;
};

struct Bus;

struct FakeHost : MomentHost {
  std::vector<NearbyCopy> peers;
  std::map<uint64_t, uint8_t> dispositions;
  SocialMode mode = SocialMode::Ambivert;
  const char* suppress = nullptr;
  bool greeting = false;
  bool sendable = true;
  std::vector<std::string> sent;
  std::vector<uint32_t> sentAt;
  std::vector<Render> renders;
  uint16_t basePx = 35;
  std::vector<Color> base{Color(255, 0, 0, 0)};
  std::vector<Color> shade{Color(0, 0, 255, 0)};
  std::vector<std::string> logs;
  uint32_t nowMs = 0;
  Bus* bus = nullptr;
  uint8_t self[6] = {0};

  const std::vector<NearbyCopy>& nearbyPeers() override { return peers; }
  uint8_t disposition(const uint8_t mac[6]) override {
    auto it = dispositions.find(macKey(mac));
    return it == dispositions.end() ? kDispositionNeutral : it->second;
  }
  SocialMode socialMode() override { return mode; }
  const char* suppressor() override { return suppress; }
  bool greetingActive() override { return greeting; }
  bool canSend() override { return sendable; }
  void send(const char* payload, size_t len) override;
  bool trigger(const ExpressionInvocation& inv, const uint8_t src[6], bool broadcast,
               TransientSource source) override {
    Render r{inv, {}, broadcast, source, nowMs};
    std::memcpy(r.src, src, 6);
    renders.push_back(r);
    return true;
  }
  uint16_t basePixels() override { return basePx; }
  const std::vector<Color>& surfaceColors(bool isBase) override { return isBase ? base : shade; }
  void log(const char* line) override { logs.emplace_back(line); }

  void like(uint8_t n, uint8_t d) {
    uint8_t m[6];
    macOf(n, m);
    dispositions[macKey(m)] = d;
  }
  size_t rendersOf(const char* type) const {
    size_t n = 0;
    for (const Render& r : renders) n += r.inv.type == type;
    return n;
  }
};

// Broadcast mesh: every frame reaches every other lamp after `latencyMs`.
struct Bus {
  struct InFlight {
    uint32_t atMs;
    size_t from;
    std::string payload;
  };
  uint32_t latencyMs = 30;
  std::vector<FakeHost*> hosts;
  std::vector<MomentDirector*> directors;
  std::deque<InFlight> flight;
  std::vector<size_t> sentBy;

  void post(FakeHost* h, const std::string& payload) {
    for (size_t i = 0; i < hosts.size(); ++i) {
      if (hosts[i] == h) {
        flight.push_back({h->nowMs + latencyMs, i, payload});
        sentBy.push_back(i);
      }
    }
  }
  void deliver(uint32_t nowMs) {
    while (!flight.empty() && timeReached(nowMs, flight.front().atMs)) {
      InFlight f = flight.front();
      flight.pop_front();
      ExpressionObserverRegistry none;
      for (size_t i = 0; i < directors.size(); ++i) {
        if (i == f.from) continue;
        hosts[i]->nowMs = nowMs;
        deliverEvent(reinterpret_cast<const uint8_t*>(f.payload.data()), f.payload.size(),
                     hosts[f.from]->self, nowMs, *directors[i], none);
      }
    }
  }
};

void FakeHost::send(const char* payload, size_t len) {
  sent.emplace_back(payload, len);
  sentAt.push_back(nowMs);
  if (bus) bus->post(this, sent.back());
}

struct Rig {
  FakeHost host;
  MomentDirector director;
  uint8_t self[6];
  uint32_t seed;

  explicit Rig(uint8_t n = 1, uint32_t rngSeed = 7, uint32_t nowMs = 0) : seed(rngSeed) {
    macOf(n, self);
    std::memcpy(host.self, self, 6);
    host.nowMs = nowMs;
    director.begin(&host, self, rngSeed, nowMs);
  }
  void run(uint32_t fromMs, uint32_t toMs, uint32_t stepMs = 10) {
    for (uint32_t t = fromMs; t <= toMs; t += stepMs) {
      host.nowMs = t;
      director.tick(t);
    }
  }
  void frame(const uint8_t src[6], const std::string& payload, uint32_t nowMs) {
    host.nowMs = nowMs;
    ExpressionObserverRegistry none;
    deliverEvent(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), src, nowMs,
                 director, none);
  }
};

std::string encode(const MomentFrame& f) {
  char buf[256];
  const size_t n = encodeMomentFrame(f, buf, sizeof(buf));
  return std::string(buf, n);
}

std::string firefliesStart(uint32_t seed, uint16_t age = 0) {
  MomentFrame f;
  f.kind = MomentKind::Fireflies;
  f.seed = seed;
  f.ageMs = age;
  return encode(f);
}

std::string whisperTo(const uint8_t to[6], uint32_t seed, std::initializer_list<uint8_t> visited) {
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = seed;
  std::memcpy(f.to, to, 6);
  for (uint8_t n : visited) macOf(n, f.visited[f.hop++]);
  return encode(f);
}

// Three liked peers too faint for a Whisper (Fond needs -62 dBm), so only
// Fireflies can qualify.
void addLikedPeers(Rig& r, uint32_t nowMs, uint8_t count = 3) {
  for (uint8_t n = 10; n < 10 + count; ++n) {
    r.host.like(n, kDispositionFond);
    r.host.peers.push_back(peerOf(n, -75, nowMs));
  }
}

// Three liked peers present from boot, so the still clock runs from t=0.
void readyForFireflies(Rig& r) {
  addLikedPeers(r, 0);
  r.director.begin(&r.host, r.self, r.seed, 0);
}

}  // namespace

namespace {

MomentFrame decodeSent(const std::string& payload) {
  JsonDocument doc;
  deserializeJson(doc, payload);
  MomentFrame f;
  TEST_ASSERT_TRUE(decodeMomentFrame(doc["mo"].as<JsonObjectConst>(), f));
  return f;
}

void runFresh(Rig& r, uint32_t from, uint32_t to, uint32_t step) {
  for (uint32_t t = from; t <= to; t += step) {
    for (NearbyCopy& p : r.host.peers) seeAt(p, t);
    r.host.nowMs = t;
    r.director.tick(t);
  }
}

}  // namespace

void setUp() {}
void tearDown() {}


void test_wait_lies_in_the_range_times_mode() {
  FastRng rng(99);
  const SocialMode modes[] = {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert};
  for (size_t k = 0; k < kMomentKindCount; ++k) {
    const MomentKind kind = static_cast<MomentKind>(k + 1);
    const MomentPacing& p = momentPacing(kind);
    for (SocialMode m : modes) {
      const uint64_t pct = momentModePercent(m);
      for (int i = 0; i < 500; ++i) {
        const uint64_t w = momentWaitMs(kind, 0, m, rng);
        TEST_ASSERT_TRUE(w >= p.loMin * kMin * pct / 100);
        TEST_ASSERT_TRUE(w <= p.hiMin * kMin * pct / 100);
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT32(150, momentModePercent(SocialMode::Introvert));
  TEST_ASSERT_EQUAL_UINT32(100, momentModePercent(SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_UINT32(70, momentModePercent(SocialMode::Extrovert));
}

void test_growth_shifts_both_ends_and_stops_at_the_cap() {
  TEST_ASSERT_EQUAL_UINT16(20, momentPacing(MomentKind::Fireflies).loMin);
  TEST_ASSERT_EQUAL_UINT16(60, momentPacing(MomentKind::Fireflies).hiMin);
  TEST_ASSERT_EQUAL_UINT16(30, momentPacing(MomentKind::Fireflies).stepMin);
  TEST_ASSERT_EQUAL_UINT16(8, momentPacing(MomentKind::Whisper).loMin);
  TEST_ASSERT_EQUAL_UINT16(10, momentPacing(MomentKind::Whisper).stepMin);
  FastRng rng(5);
  for (int i = 0; i < 300; ++i) {
    const uint32_t w = momentWaitMs(MomentKind::Fireflies, 6, SocialMode::Ambivert, rng);
    TEST_ASSERT_TRUE(w >= 200 * kMin && w <= 240 * kMin);
    const uint32_t capped = momentWaitMs(MomentKind::Fireflies, 9, SocialMode::Ambivert, rng);
    TEST_ASSERT_TRUE(capped >= 200 * kMin && capped <= 240 * kMin);
    const uint32_t wh = momentWaitMs(MomentKind::Whisper, 2, SocialMode::Ambivert, rng);
    TEST_ASSERT_TRUE(wh >= 28 * kMin && wh <= 40 * kMin);
  }
  uint8_t g = 0;
  for (int i = 0; i < 10; ++i) g = momentGrow(MomentKind::Fireflies, g);
  TEST_ASSERT_EQUAL_UINT8(6, g);
}

void test_participation_grows_and_rerolls_from_now() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT8(0, r.director.growth(MomentKind::Whisper));
  TEST_ASSERT_TRUE(r.director.force(MomentKind::Whisper, 11, 1000));
  TEST_ASSERT_EQUAL_UINT8(1, r.director.growth(MomentKind::Whisper));
  TEST_ASSERT_FALSE(r.director.eligible(MomentKind::Whisper, 1000 + 18 * kMin - 1));
  TEST_ASSERT_TRUE(r.director.eligible(MomentKind::Whisper, 1000 + 30 * kMin));
}

void test_state_resets_on_begin() {
  Rig r;
  r.director.force(MomentKind::Whisper, 11, 1000);
  r.director.begin(&r.host, r.self, 3, 2000);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT8(0, r.director.growth(MomentKind::Whisper));
  TEST_ASSERT_FALSE(r.director.coolingDown(MomentKind::Whisper, 2000));
  TEST_ASSERT_FALSE(r.director.eligible(MomentKind::Fireflies, 2000));
}

void test_mode_change_does_not_reroll() {
  Rig r(1, 42, 0);
  uint32_t at = 0;
  while (!r.director.eligible(MomentKind::Fireflies, at)) at += 1000;
  r.host.mode = SocialMode::Introvert;
  r.run(0, at - 1000, 1000);
  TEST_ASSERT_FALSE(r.director.eligible(MomentKind::Fireflies, at - 1000));
  TEST_ASSERT_TRUE(r.director.eligible(MomentKind::Fireflies, at));
}


void test_hearing_a_frame_sets_the_crowd_cooldown_with_max() {
  Rig r;
  uint8_t other[6];
  macOf(50, other);
  r.host.like(50, 1);  // Salty: hears but never joins
  r.frame(other, firefliesStart(5), 10 * kMin);
  TEST_ASSERT_TRUE(r.director.coolingDown(MomentKind::Fireflies, 54 * kMin));
  TEST_ASSERT_FALSE(r.director.coolingDown(MomentKind::Whisper, 10 * kMin));
  // A frame stamped earlier never shortens the window.
  r.frame(other, firefliesStart(6), 5 * kMin);
  TEST_ASSERT_TRUE(r.director.coolingDown(MomentKind::Fireflies, 54 * kMin));
  TEST_ASSERT_FALSE(r.director.coolingDown(MomentKind::Fireflies, 55 * kMin + 1));
}

void test_cooldown_blocks_initiation_but_not_joining() {
  Rig r(1, 9, 0);
  readyForFireflies(r);
  uint8_t far[6];
  macOf(60, far);
  r.host.like(60, 1);
  const uint32_t t = 100 * kMin;
  r.frame(far, firefliesStart(1), t);  // heard, refused: cooling now
  TEST_ASSERT_FALSE(r.director.active());
  for (NearbyCopy& p : r.host.peers) seeAt(p, t);
  r.run(t, t + 120000, 100);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_TRUE(r.host.sent.empty());

  uint8_t friendly[6];
  macOf(61, friendly);
  r.host.like(61, kDispositionFond);
  r.frame(friendly, firefliesStart(2), t + 130000);
  TEST_ASSERT_TRUE(r.director.active());
  TEST_ASSERT_TRUE(r.director.role() == MomentDirector::Role::Joiner);
}

void test_cooldown_does_not_block_accepting_a_whisper() {
  Rig r;
  uint8_t sender[6];
  macOf(70, sender);
  r.host.like(70, kDispositionFond);
  uint8_t bystander[6];
  macOf(71, bystander);
  r.frame(sender, whisperTo(bystander, 3, {70}), 1000);  // overheard: cooling
  TEST_ASSERT_TRUE(r.director.coolingDown(MomentKind::Whisper, 1000));
  TEST_ASSERT_FALSE(r.director.active());
  r.frame(sender, whisperTo(r.self, 4, {70}), 2000);
  TEST_ASSERT_TRUE(r.director.active());
  TEST_ASSERT_TRUE(r.director.role() == MomentDirector::Role::Accepter);
}

void test_initiates_after_eligibility_and_backoff() {
  Rig r(1, 9, 0);
  readyForFireflies(r);
  const uint32_t t = 100 * kMin;
  for (NearbyCopy& p : r.host.peers) seeAt(p, t);
  r.host.greeting = true;
  r.run(t, t + 100000, 100);
  TEST_ASSERT_FALSE(r.director.active());
  r.host.greeting = false;
  r.run(t + 100000, t + 200000, 100);
  TEST_ASSERT_TRUE(r.director.active());
  TEST_ASSERT_TRUE(r.director.activeKind() == MomentKind::Fireflies);
  TEST_ASSERT_EQUAL_UINT32(2, r.host.sent.size());
}

void test_a_lapse_disarms_the_backoff() {
  Rig r(1, 9, 0);
  readyForFireflies(r);
  const uint32_t t = 100 * kMin;
  uint32_t now = t;
  for (; !r.director.backoffArmed(MomentKind::Fireflies); now += 1000) {
    for (NearbyCopy& p : r.host.peers) seeAt(p, now);
    r.director.tick(now);
    TEST_ASSERT_TRUE(now < t + 5000);
  }
  r.host.greeting = true;
  r.director.tick(now + 1000);
  TEST_ASSERT_FALSE(r.director.backoffArmed(MomentKind::Fireflies));
}

void test_suppressors_block_initiation() {
  const char* reasons[] = {"ota", "wisp", "preview", "home"};
  for (const char* why : reasons) {
    Rig r(1, 9, 0);
    readyForFireflies(r);
    r.host.like(30, kDispositionSmitten);
    r.host.peers.push_back(peerOf(30, -50, 0));
    const uint32_t t = 100 * kMin;
    r.host.suppress = why;
    for (uint32_t now = t; now <= t + 100000; now += 100) {
      for (NearbyCopy& p : r.host.peers) seeAt(p, now);
      r.host.nowMs = now;
      r.director.tick(now);
    }
    TEST_ASSERT_FALSE_MESSAGE(r.director.active(), why);
    TEST_ASSERT_TRUE_MESSAGE(r.host.sent.empty(), why);
    TEST_ASSERT_TRUE_MESSAGE(r.host.renders.empty(), why);
  }
}


namespace {

struct Herd {
  std::vector<std::unique_ptr<Rig>> rigs;
  Bus bus;
};

// Every lamp in the room eligible and ready for `kind` at once; returns how
// many of them started one.
size_t runHerd(MomentKind kind, size_t lamps, uint32_t runSeed) {
  Herd herd;
  for (size_t i = 0; i < lamps; ++i) {
    auto rig = std::make_unique<Rig>(static_cast<uint8_t>(i), runSeed * 7919u + i * 104729u + 1, 0);
    if (kind == MomentKind::Fireflies) {
      readyForFireflies(*rig);
      for (size_t j = 0; j < lamps; ++j) rig->host.like(static_cast<uint8_t>(j), kDispositionFond);
    } else {
      // Peers arrive after boot, so the Fireflies still clock is not yet run.
      for (size_t j = 0; j < lamps; ++j) {
        if (j == i) continue;
        rig->host.like(static_cast<uint8_t>(j), kDispositionFond);
        rig->host.peers.push_back(peerOf(static_cast<uint8_t>(j), -55, 0));
      }
    }
    rig->host.bus = &herd.bus;
    herd.bus.hosts.push_back(&rig->host);
    herd.bus.directors.push_back(&rig->director);
    herd.rigs.push_back(std::move(rig));
  }
  const uint32_t start = 100 * kMin;
  for (uint32_t t = start;; t += 5) {
    herd.bus.deliver(t);
    bool armed = false;
    for (auto& rig : herd.rigs) {
      for (NearbyCopy& p : rig->host.peers) seeAt(p, t);
      rig->host.nowMs = t;
      rig->director.tick(t);
      armed = armed || rig->director.backoffArmed(kind);
    }
    if (t > start + 1000 && !armed && herd.bus.flight.empty()) break;
    TEST_ASSERT_TRUE(t < start + 200000);
  }
  char prefix[32];
  std::snprintf(prefix, sizeof(prefix), "[moment] start k=%u", static_cast<unsigned>(kind));
  size_t starts = 0;
  for (auto& rig : herd.rigs) {
    for (const std::string& line : rig->host.logs) starts += line.rfind(prefix, 0) == 0;
  }
  return starts;
}

void assertHerd(MomentKind kind, size_t lamps) {
  constexpr int kRuns = 1000;
  int exactlyOne = 0;
  size_t worst = 0;
  for (int run = 0; run < kRuns; ++run) {
    const size_t n = runHerd(kind, lamps, static_cast<uint32_t>(run + 1));
    exactlyOne += n == 1;
    worst = n > worst ? n : worst;
  }
  char summary[64];
  std::snprintf(summary, sizeof(summary), "k=%u lamps=%u single %d/%d worst %u",
                static_cast<unsigned>(kind), static_cast<unsigned>(lamps), exactlyOne, kRuns,
                static_cast<unsigned>(worst));
  TEST_MESSAGE(summary);
  TEST_ASSERT_TRUE_MESSAGE(exactlyOne * 100 >= kRuns * 95, summary);
  TEST_ASSERT_TRUE_MESSAGE(worst <= 2, summary);
}

}  // namespace

void test_herd_starts_one_fireflies_in_a_room_of_fifty() { assertHerd(MomentKind::Fireflies, 50); }
void test_herd_starts_one_whisper_among_five() { assertHerd(MomentKind::Whisper, 5); }
void test_herd_starts_one_whisper_among_ten() { assertHerd(MomentKind::Whisper, 10); }
void test_herd_starts_one_whisper_among_twenty() { assertHerd(MomentKind::Whisper, 20); }

void test_fireflies_period_and_first_offset_ranges() {
  uint8_t mac[6];
  for (uint32_t s = 0; s < 5000; s += 7) {
    const uint32_t t = firefliesPeriodMs(s * 2654435761u);
    TEST_ASSERT_TRUE(t >= 1000 && t < 1600);
    macOf(static_cast<uint8_t>(s), mac);
    const int32_t d = firefliesFirstOffset(s * 2654435761u, mac);
    TEST_ASSERT_TRUE(d >= -static_cast<int32_t>(t / 2));
    TEST_ASSERT_TRUE(d < static_cast<int32_t>(t - t / 2));
  }
}

void test_fireflies_offset_shrinks_and_reaches_zero_by_84() {
  for (int32_t d1 : {799, -800, 512, -37, 11, -10, 0}) {
    int32_t d = d1;
    for (uint16_t k = 1; k <= kFirefliesBlinks; ++k) {
      const int32_t next = firefliesNextOffset(d, k);
      TEST_ASSERT_TRUE(std::abs(next) <= std::abs(d));
      if (k < 9) TEST_ASSERT_EQUAL_INT32(d, next);
      d = next;
      if (k + 1 >= 84) TEST_ASSERT_EQUAL_INT32_MESSAGE(0, d, "not converged by blink 84");
    }
  }
}

void test_fireflies_two_lamps_land_on_one_beat() {
  uint8_t a[6], b[6];
  macOf(1, a);
  macOf(2, b);
  const uint32_t seed = 3405691582u;
  const uint32_t t = firefliesPeriodMs(seed);
  int32_t da = firefliesFirstOffset(seed, a);
  int32_t db = firefliesFirstOffset(seed, b);
  TEST_ASSERT_NOT_EQUAL(da, db);
  for (uint16_t k = 1; k < kFirefliesBlinks; ++k) {
    da = firefliesNextOffset(da, k);
    db = firefliesNextOffset(db, k);
  }
  TEST_ASSERT_EQUAL_UINT32(firefliesBlinkMs(0, t, kFirefliesBlinks, da),
                           firefliesBlinkMs(0, t, kFirefliesBlinks, db));
}

// Known vectors: d_1..d_120 for seed 42 on two MACs, one starting late and one
// early. A bench lamp's `[moment] ff k= d=` lines come from this arithmetic.
const int32_t kSeed42Mac1[kFirefliesBlinks] = {
    132, 132, 132, 132, 132, 132, 132, 132, 132, 124, 117, 110,
    103, 97, 91, 86, 81, 76, 71, 67, 63, 59, 55, 52,
    49, 46, 43, 40, 38, 36, 34, 32, 30, 28, 26, 24,
    23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12,
    11, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const int32_t kSeed42Mac2[kFirefliesBlinks] = {
    -182, -182, -182, -182, -182, -182, -182, -182, -182, -171, -161, -151,
    -142, -133, -125, -118, -111, -104, -98, -92, -86, -81, -76, -71,
    -67, -63, -59, -55, -52, -49, -46, -43, -40, -38, -36, -34,
    -32, -30, -28, -26, -24, -23, -22, -21, -20, -19, -18, -17,
    -16, -15, -14, -13, -12, -11, -10, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void test_fireflies_known_vectors() {
  TEST_ASSERT_EQUAL_UINT32(0, momentHash(0));
  TEST_ASSERT_EQUAL_UINT32(1364076727u, momentHash(1));
  TEST_ASSERT_EQUAL_UINT32(233162409u, momentHash(0xDEADBEEFu));
  TEST_ASSERT_EQUAL_UINT32(1042, firefliesPeriodMs(42));
  const int32_t* expected[] = {kSeed42Mac1, kSeed42Mac2};
  for (uint8_t n = 1; n <= 2; ++n) {
    uint8_t mac[6];
    macOf(n, mac);
    int32_t d = firefliesFirstOffset(42, mac);
    for (uint16_t k = 1; k <= kFirefliesBlinks; ++k) {
      TEST_ASSERT_EQUAL_INT32(expected[n - 1][k - 1], d);
      d = firefliesNextOffset(d, k);
    }
  }
}

void test_fireflies_envelope_endpoints() {
  TEST_ASSERT_EQUAL_UINT8(10, firefliesOpacity(1));
  TEST_ASSERT_EQUAL_UINT8(25, firefliesOpacity(6));
  TEST_ASSERT_EQUAL_UINT8(25, firefliesOpacity(7));
  TEST_ASSERT_EQUAL_UINT8(25, firefliesOpacity(110));
  TEST_ASSERT_TRUE(firefliesOpacity(111) < 25);
  TEST_ASSERT_EQUAL_UINT8(10, firefliesOpacity(120));
}

void test_fireflies_timeline_is_deterministic() {
  Rig a(3, 1, 0), b(3, 2, 0);
  a.director.force(MomentKind::Fireflies, 777, 1000);
  b.director.force(MomentKind::Fireflies, 777, 1000);
  a.run(1000, 1000 + 200000, 5);
  b.run(1000, 1000 + 200000, 5);
  TEST_ASSERT_EQUAL_UINT32(kFirefliesBlinks, a.host.rendersOf("flicker"));
  TEST_ASSERT_EQUAL_UINT32(a.host.renders.size(), b.host.renders.size());
  for (size_t i = 0; i < a.host.renders.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(a.host.renders[i].atMs, b.host.renders[i].atMs);
  }
  TEST_ASSERT_FALSE(a.director.active());
}

void test_fireflies_late_joiner_recovers_t0_from_age() {
  Rig early(4, 1, 0), late(5, 2, 0);
  uint8_t origin[6];
  macOf(9, origin);
  early.host.like(9, kDispositionFond);
  late.host.like(9, kDispositionFond);
  early.frame(origin, firefliesStart(1234, 0), 10000);
  late.frame(origin, firefliesStart(1234, 1500), 11500);
  TEST_ASSERT_TRUE(early.director.active());
  TEST_ASSERT_TRUE(late.director.active());
  early.run(10000, 400000, 5);
  late.run(11500, 400000, 5);
  // Both converge, so their last blinks fall within one 5 ms tick.
  const uint32_t ea = early.host.renders.back().atMs;
  const uint32_t la = late.host.renders.back().atMs;
  TEST_ASSERT_UINT32_WITHIN(5, ea, la);
}


void test_fireflies_join_gates() {
  uint8_t origin[6];
  macOf(9, origin);
  for (uint8_t d = 1; d <= 2; ++d) {
    Rig r;
    r.host.like(9, d);
    r.frame(origin, firefliesStart(5), 1000);
    TEST_ASSERT_FALSE(r.director.active());
  }
  {
    Rig r;
    r.frame(origin, firefliesStart(5), 1000);  // unknown peer reads as Neutral
    TEST_ASSERT_TRUE(r.director.active());
  }
  const char* reasons[] = {"ota", "wisp", "preview", "home"};
  for (const char* why : reasons) {
    Rig r;
    r.host.suppress = why;
    r.frame(origin, firefliesStart(5), 1000);
    TEST_ASSERT_FALSE_MESSAGE(r.director.active(), why);
  }
  {
    Rig r;
    r.director.force(MomentKind::Fireflies, 3, 500);
    r.frame(origin, firefliesStart(5), 1000);
    TEST_ASSERT_TRUE(r.director.role() == MomentDirector::Role::Initiator);
  }
}

void test_fireflies_resent_copy_is_ignored() {
  Rig r;
  uint8_t origin[6];
  macOf(9, origin);
  r.frame(origin, firefliesStart(5), 1000);
  const uint8_t g = r.director.growth(MomentKind::Fireflies);
  r.run(1000, 200000, 5);
  TEST_ASSERT_FALSE(r.director.active());
  r.frame(origin, firefliesStart(5, 200), 200100);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT8(g, r.director.growth(MomentKind::Fireflies));
}


void test_liked_fingerprint_tracks_the_liked_set_only() {
  std::map<uint64_t, uint8_t> d;
  auto disp = [&](const uint8_t* m) {
    auto it = d.find(macKey(m));
    return it == d.end() ? kDispositionNeutral : it->second;
  };
  std::vector<NearbyCopy> peers = {peerOf(1, -60, 0), peerOf(2, -60, 0)};
  d[macKey(peers[0].mac)] = kDispositionFond;
  const uint32_t one = likedFingerprint(peers, disp);
  peers.push_back(peerOf(3, -60, 0));  // a Neutral arrival
  TEST_ASSERT_EQUAL_UINT32(one, likedFingerprint(peers, disp));
  d[macKey(peers[2].mac)] = kDispositionSmitten;  // disposition change
  const uint32_t two = likedFingerprint(peers, disp);
  TEST_ASSERT_NOT_EQUAL(one, two);
  peers.push_back(peerOf(4, -60, 0));
  d[macKey(peers[3].mac)] = kDispositionFond;  // a liked arrival
  const uint32_t three = likedFingerprint(peers, disp);
  TEST_ASSERT_NOT_EQUAL(two, three);
  peers.pop_back();  // departure
  TEST_ASSERT_EQUAL_UINT32(two, likedFingerprint(peers, disp));
}

void test_ten_still_minutes_make_fireflies_possible() {
  Rig r(1, 9, 0);
  r.run(0, 100 * kMin, 1000);  // eligible, no liked peers yet
  TEST_ASSERT_FALSE(r.director.active());
  const uint32_t t = 100 * kMin;
  addLikedPeers(r, t);
  r.run(t, t + 9 * kMin, 1000);
  TEST_ASSERT_FALSE(r.director.backoffArmed(MomentKind::Fireflies));
  TEST_ASSERT_FALSE(r.director.active());
  // A Neutral peer coming and going does not reset the clock.
  r.host.peers.push_back(peerOf(40, -60, t));
  r.run(t + 9 * kMin, t + 10 * kMin + 2000, 1000);
  TEST_ASSERT_TRUE(r.director.backoffArmed(MomentKind::Fireflies) || r.director.active());
}

// The liked set unchanged for a whole wrap while a suppressor holds moments
// off: once it lifts, the still clock has run, not restarted at the wrap.
void test_still_clock_stays_run_a_wrap_later() {
  Rig r(1, 9, 0);
  readyForFireflies(r);
  r.host.suppress = "home";
  auto tickAt = [&](uint32_t now) {
    for (NearbyCopy& p : r.host.peers) seeAt(p, now);
    r.host.nowMs = now;
    r.director.tick(now);
  };
  for (uint64_t t = 1ull << 28; t < (1ull << 32); t += 1ull << 28) {
    tickAt(static_cast<uint32_t>(t));
  }
  r.host.suppress = nullptr;
  tickAt(5000);
  TEST_ASSERT_TRUE(r.director.backoffArmed(MomentKind::Fireflies) || r.director.active());
}

void test_a_liked_departure_resets_the_still_clock() {
  Rig r(1, 9, 0);
  const uint32_t t = 100 * kMin;
  r.run(0, t, 1000);
  addLikedPeers(r, t);
  r.host.peers.push_back(peerOf(13, -60, t));
  r.host.like(13, kDispositionFond);
  r.run(t, t + 9 * kMin, 1000);
  r.host.peers.pop_back();
  r.run(t + 9 * kMin, t + 11 * kMin, 1000);
  TEST_ASSERT_FALSE(r.director.backoffArmed(MomentKind::Fireflies));
  TEST_ASSERT_FALSE(r.director.active());
}


void test_next_hop_picks_the_strongest_liked_fresh_unvisited() {
  std::map<uint64_t, uint8_t> d;
  auto disp = [&](const uint8_t* m) {
    auto it = d.find(macKey(m));
    return it == d.end() ? kDispositionNeutral : it->second;
  };
  const uint32_t now = 100000;
  std::vector<NearbyCopy> peers = {
      peerOf(1, -40, now),                     // Neutral: not liked
      peerOf(2, -50, now - 20000),             // stale
      peerOf(3, -85, now),                     // below the floor
      peerOf(4, -55, now),                     // visited
      peerOf(5, -60, now),                     // the pick
      peerOf(6, -70, now),
  };
  for (uint8_t n = 2; n <= 6; ++n) d[macKey(peers[n - 1].mac)] = kDispositionFond;
  uint8_t self[6], visited[1][6], out[6];
  macOf(99, self);
  macOf(4, visited[0]);
  TEST_ASSERT_TRUE(selectWhisperHop(peers, now, self, visited, 1, disp, out));
  TEST_ASSERT_EQUAL_UINT8(5, out[5]);
}

void test_next_hop_ties_go_to_smitten_then_lower_mac() {
  std::map<uint64_t, uint8_t> d;
  auto disp = [&](const uint8_t* m) { return d[macKey(m)]; };
  const uint32_t now = 100000;
  std::vector<NearbyCopy> peers = {peerOf(7, -60, now), peerOf(8, -60, now), peerOf(6, -60, now)};
  d[macKey(peers[0].mac)] = kDispositionFond;
  d[macKey(peers[1].mac)] = kDispositionSmitten;
  d[macKey(peers[2].mac)] = kDispositionFond;
  uint8_t self[6], out[6];
  macOf(99, self);
  TEST_ASSERT_TRUE(selectWhisperHop(peers, now, self, nullptr, 0, disp, out));
  TEST_ASSERT_EQUAL_UINT8(8, out[5]);
  d[macKey(peers[1].mac)] = kDispositionFond;
  TEST_ASSERT_TRUE(selectWhisperHop(peers, now, self, nullptr, 0, disp, out));
  TEST_ASSERT_EQUAL_UINT8(6, out[5]);
}

void test_no_candidate_ends_the_chain() {
  Rig r;
  r.host.peers.push_back(peerOf(20, -60, 0));  // Neutral
  r.director.force(MomentKind::Whisper, 3, 0);
  r.run(0, 10000, 10);
  TEST_ASSERT_TRUE(r.host.sent.empty());
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT32(1, r.host.rendersOf("pulse"));
}

void test_hop_eight_does_not_forward() {
  Rig r(9);
  r.host.like(30, kDispositionFond);
  r.host.like(8, kDispositionFond);
  r.host.peers.push_back(peerOf(30, -50, 0));
  uint8_t sender[6];
  macOf(8, sender);
  r.frame(sender, whisperTo(r.self, 5, {1, 2, 3, 4, 5, 6, 7, 8}), 1000);
  TEST_ASSERT_TRUE(r.director.active());
  for (NearbyCopy& p : r.host.peers) seeAt(p, 4000);
  r.run(1000, 6000, 10);
  TEST_ASSERT_TRUE(r.host.sent.empty());

  Rig r7(9);
  r7.host.like(30, kDispositionFond);
  r7.host.peers.push_back(peerOf(30, -50, 4000));
  r7.frame(sender, whisperTo(r7.self, 5, {1, 2, 3, 4, 5, 6, 8}), 1000);
  r7.run(1000, 6000, 10);
  TEST_ASSERT_EQUAL_UINT32(2, r7.host.sent.size());
  TEST_ASSERT_EQUAL_UINT8(8, decodeSent(r7.host.sent[0]).hop);
}

void test_malformed_whisper_frames_are_dropped() {
  Rig r(9);
  uint8_t sender[6];
  macOf(8, sender);
  const char* bad[] = {
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":0,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":9,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001240AC4102001240AC4102001240AC4102001240AC4102001240AC4102001240AC4102001240AC4102001240AC4102001\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":2,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001240AC4102002\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC41020ZZ\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC41020\",\"v\":\"240AC4102001\"}}",
      "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":2001,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\"}}",
  };
  for (const char* b : bad) {
    r.frame(sender, b, 1000);
    TEST_ASSERT_FALSE_MESSAGE(r.director.active(), b);
    TEST_ASSERT_FALSE_MESSAGE(r.director.coolingDown(MomentKind::Whisper, 1000), b);
  }
  r.frame(sender, "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\"}}", 1000);
  TEST_ASSERT_TRUE(r.director.active());
}

void test_duplicate_whisper_is_ignored() {
  Rig r(9);
  uint8_t sender[6];
  macOf(8, sender);
  r.frame(sender, whisperTo(r.self, 5, {1}), 1000);
  TEST_ASSERT_TRUE(r.director.active());
  r.run(1000, 6000, 10);
  TEST_ASSERT_FALSE(r.director.active());
  r.frame(sender, whisperTo(r.self, 5, {1}), 7000);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT32(1, r.host.rendersOf("pulse"));
  r.frame(sender, whisperTo(r.self, 6, {1}), 8000);  // a new Whisper
  TEST_ASSERT_TRUE(r.director.active());
}

void test_wary_toward_the_sender_drops_the_whisper() {
  Rig r(9);
  uint8_t sender[6];
  macOf(8, sender);
  r.host.like(8, 2);
  r.frame(sender, whisperTo(r.self, 5, {1, 8}), 1000);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT32(0, r.host.renders.size());
}

void test_whisper_suppressors() {
  const char* reasons[] = {"ota", "wisp", "preview", "home"};
  for (const char* why : reasons) {
    Rig r(9);
    uint8_t sender[6];
    macOf(8, sender);
    r.host.suppress = why;
    r.frame(sender, whisperTo(r.self, 5, {8}), 1000);
    TEST_ASSERT_FALSE_MESSAGE(r.director.active(), why);
    TEST_ASSERT_FALSE_MESSAGE(r.director.force(MomentKind::Whisper, 1, 1000), why);
  }
}

// Ten lamps in a ring, each liking only its two neighbours, every one ready.
void test_ring_of_ten_never_revisits_and_ends_within_eight_hops() {
  constexpr uint8_t kLamps = 10;
  Bus bus;
  std::vector<std::unique_ptr<Rig>> rigs;
  for (uint8_t i = 0; i < kLamps; ++i) {
    auto rig = std::make_unique<Rig>(i, 100 + i, 0);
    for (uint8_t nb : {static_cast<uint8_t>((i + 1) % kLamps),
                       static_cast<uint8_t>((i + kLamps - 1) % kLamps)}) {
      rig->host.like(nb, kDispositionFond);
      rig->host.peers.push_back(peerOf(nb, nb == (i + 1) % kLamps ? -50 : -55, 0));
    }
    rig->host.bus = &bus;
    bus.hosts.push_back(&rig->host);
    bus.directors.push_back(&rig->director);
    rigs.push_back(std::move(rig));
  }
  rigs[0]->director.force(MomentKind::Whisper, 4242, 0);
  for (uint32_t t = 0; t < 60000; t += 10) {
    bus.deliver(t);
    for (auto& rig : rigs) {
      for (NearbyCopy& p : rig->host.peers) seeAt(p, t);
      rig->host.nowMs = t;
      rig->director.tick(t);
    }
  }
  std::vector<size_t> glows;
  for (size_t i = 0; i < rigs.size(); ++i) {
    const size_t n = rigs[i]->host.rendersOf("pulse");
    TEST_ASSERT_TRUE_MESSAGE(n <= 1, "a lamp glowed twice");
    if (n) glows.push_back(i);
  }
  TEST_ASSERT_EQUAL_UINT32(9, glows.size());  // origin + 8 hops
  size_t hopFrames = 0;
  for (auto& rig : rigs) hopFrames += rig->host.sent.size();
  TEST_ASSERT_EQUAL_UINT32(16, hopFrames);  // 8 hops, each sent twice
  for (auto& rig : rigs) {
    for (const std::string& p : rig->host.sent) TEST_ASSERT_TRUE(decodeSent(p).hop <= kWhisperMaxHops);
  }
}


void test_hop_eight_payload_fits_the_event_frame() {
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 0xFFFFFFFFu;
  f.ageMs = 2000;
  f.hop = 8;
  f.hasColor = true;
  f.color = Color(255, 255, 255, 255);
  for (uint8_t i = 0; i < 8; ++i) macOf(0xF0 + i, f.visited[i]);
  macOf(0xFF, f.to);
  char buf[lamp_protocol::EVENT_MAX_PAYLOAD];
  const size_t n = encodeMomentFrame(f, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n <= lamp_protocol::EVENT_MAX_PAYLOAD);
  char size[24];
  std::snprintf(size, sizeof(size), "worst case %u B", static_cast<unsigned>(n));
  TEST_MESSAGE(size);
  TEST_ASSERT_TRUE(n <= 210);
}

void test_codec_round_trips() {
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 3405691582u;
  f.ageMs = 1234;
  f.hop = 3;
  for (uint8_t i = 0; i < 3; ++i) macOf(i + 1, f.visited[i]);
  macOf(77, f.to);
  f.hasColor = true;
  f.color = Color(0x12, 0xAB, 0x00, 0xFF);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, encode(f)) == DeserializationError::Ok);
  MomentFrame back;
  TEST_ASSERT_TRUE(decodeMomentFrame(doc["mo"].as<JsonObjectConst>(), back));
  TEST_ASSERT_TRUE(back.kind == MomentKind::Whisper);
  TEST_ASSERT_EQUAL_UINT32(f.seed, back.seed);
  TEST_ASSERT_EQUAL_UINT16(1234, back.ageMs);
  TEST_ASSERT_EQUAL_UINT8(3, back.hop);
  TEST_ASSERT_EQUAL_MEMORY(f.to, back.to, 6);
  TEST_ASSERT_EQUAL_MEMORY(f.visited, back.visited, 18);
  TEST_ASSERT_TRUE(back.hasColor);
  TEST_ASSERT_TRUE(back.color == f.color);

  MomentFrame ff;
  ff.kind = MomentKind::Fireflies;
  ff.seed = 42;
  ff.ageMs = 7;
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"moment\",\"mo\":{\"k\":1,\"s\":42,\"a\":7}}", encode(ff).c_str());
}

void test_unknown_kind_is_ignored() {
  Rig r;
  uint8_t src[6];
  macOf(9, src);
  r.frame(src, "{\"type\":\"moment\",\"mo\":{\"k\":3,\"s\":1,\"a\":0}}", 1000);
  r.frame(src, "{\"type\":\"moment\",\"mo\":{\"k\":9,\"s\":1,\"a\":0}}", 1000);
  TEST_ASSERT_FALSE(r.director.active());
  for (size_t k = 1; k <= kMomentKindCount; ++k) {
    TEST_ASSERT_FALSE(r.director.coolingDown(static_cast<MomentKind>(k), 1000));
  }
}

// An older lamp parses a Moment frame as an invocation of an unknown type.
void test_older_lamps_parse_the_sentinel() {
  JsonDocument doc;
  deserializeJson(doc, firefliesStart(1));
  ExpressionInvocation inv;
  TEST_ASSERT_TRUE(parseInvocation(doc.as<JsonObjectConst>(), inv));
  TEST_ASSERT_EQUAL_STRING("moment", inv.type.c_str());
}


void test_renders_are_remote_terminal_and_on_the_base() {
  Rig r;
  r.host.like(30, kDispositionSmitten);
  r.host.peers.push_back(peerOf(30, -50, 0));
  r.director.force(MomentKind::Fireflies, 99, 0);
  r.run(0, 5000, 5);
  r.host.nowMs = 6000;
  r.director.begin(&r.host, r.self, 1, 6000);
  r.director.force(MomentKind::Whisper, 98, 6000);
  size_t kinds[2] = {0, 0};
  for (const Render& rd : r.host.renders) {
    TEST_ASSERT_TRUE(rd.source == TransientSource::Remote);
    TEST_ASSERT_FALSE(rd.broadcast);
    TEST_ASSERT_EQUAL_MEMORY(r.self, rd.src, 6);
    TEST_ASSERT_EQUAL_UINT8(TARGET_BASE, rd.inv.target);
    TEST_ASSERT_FALSE(rd.inv.colors.empty());
    if (rd.atMs < 6000) ++kinds[0];
    else ++kinds[1];
  }
  TEST_ASSERT_TRUE(kinds[0] > 0 && kinds[1] > 0);
  const Render& blink = r.host.renders.front();
  TEST_ASSERT_EQUAL_STRING("flicker", blink.inv.type.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, blink.inv.parameters.get("fire", 9));
  TEST_ASSERT_EQUAL_UINT32(firefliesStars(35), blink.inv.parameters.get("count", 0));
  TEST_ASSERT_EQUAL_UINT16(8, firefliesStars(35));  // a quarter of a 35-px base
  TEST_ASSERT_EQUAL_UINT16(1, firefliesStars(2));
  TEST_ASSERT_EQUAL_UINT32(10, blink.inv.parameters.get("opacity", 0));
}

void test_fireflies_take_the_shade_colours() {
  Rig r;
  r.host.shade = {Color(0, 200, 0, 0), Color(0, 0, 200, 0)};
  r.director.force(MomentKind::Fireflies, 99, 0);
  r.run(0, 3000, 5);
  const Render& blink = r.host.renders.front();
  TEST_ASSERT_EQUAL_UINT32(2, blink.inv.colors.size());
  TEST_ASSERT_TRUE(blink.inv.colors[0] == Color(0, 200, 0, 0));
  TEST_ASSERT_TRUE(blink.inv.colors[1] == Color(0, 0, 200, 0));
}

// The origin glows in its shade colours and carries its base colour down the
// chain; every hop glows in that colour.
void test_whisper_takes_the_origins_colour() {
  Rig origin(1);
  origin.host.base = {Color(220, 0, 90, 0)};
  origin.host.shade = {Color(0, 180, 0, 0)};
  origin.host.like(2, kDispositionFond);
  origin.host.peers.push_back(peerOf(2, -50, 0));
  origin.director.force(MomentKind::Whisper, 5, 0);
  TEST_ASSERT_TRUE(origin.host.renders[0].inv.colors[0] == Color(0, 180, 0, 0));
  const MomentFrame first = decodeSent(origin.host.sent[0]);
  TEST_ASSERT_TRUE(first.hasColor);
  TEST_ASSERT_TRUE(first.color == Color(220, 0, 90, 0));

  Rig hop(2);
  hop.host.like(3, kDispositionFond);
  hop.host.peers.push_back(peerOf(3, -50, 0));
  hop.frame(origin.self, origin.host.sent[0], 30);
  for (uint32_t t = 30; t <= 4000; t += 10) {
    seeAt(hop.host.peers[0], t);
    hop.host.nowMs = t;
    hop.director.tick(t);
  }
  TEST_ASSERT_EQUAL_UINT32(1, hop.host.rendersOf("pulse"));
  TEST_ASSERT_TRUE(hop.host.renders[0].inv.colors[0] == Color(220, 0, 90, 0));
  TEST_ASSERT_TRUE(decodeSent(hop.host.sent[0]).color == Color(220, 0, 90, 0));
}

void test_a_colour_matching_the_base_is_whitened() {
  Rig r;
  r.host.base = {Color(250, 5, 0, 0)};
  r.host.shade = {Color(255, 0, 0, 0), Color(0, 255, 0, 0)};
  r.director.force(MomentKind::Whisper, 3, 0);
  const Render& glow = r.host.renders.back();
  TEST_ASSERT_TRUE(glow.inv.colors[0] == bloomWhiten(Color(255, 0, 0, 0), 102));
  TEST_ASSERT_TRUE(glow.inv.colors[1] == Color(0, 255, 0, 0));
}

void test_a_malformed_origin_colour_drops_the_frame() {
  Rig r(9);
  uint8_t sender[6];
  macOf(8, sender);
  r.frame(sender, "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\",\"c\":\"zz00ff00\"}}", 1000);
  TEST_ASSERT_FALSE(r.director.active());
  r.frame(sender, "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\",\"c\":\"ff00\"}}", 1000);
  TEST_ASSERT_FALSE(r.director.active());
  r.frame(sender, "{\"type\":\"moment\",\"mo\":{\"k\":2,\"s\":1,\"h\":1,\"a\":0,\"to\":\"240AC4102009\",\"v\":\"240AC4102001\",\"c\":\"ff00ff00ff\"}}", 1000);
  TEST_ASSERT_FALSE(r.director.active());
}

struct MirrorSpy : IExpressionObserver {
  int calls = 0;
  void onPeerExpression(const uint8_t*, const ExpressionInvocation&) override { ++calls; }
};

void test_moment_frames_never_reach_the_mirror() {
  Rig r;
  ExpressionObserverRegistry registry;
  MirrorSpy spy;
  registry.registerObserver(&spy);
  uint8_t src[6];
  macOf(9, src);
  const std::string moment = firefliesStart(5);
  deliverEvent(reinterpret_cast<const uint8_t*>(moment.data()), moment.size(), src, 1000,
               r.director, registry);
  TEST_ASSERT_EQUAL_INT(0, spy.calls);
  TEST_ASSERT_TRUE(r.director.active());
  const std::string plain = "{\"type\":\"pulse\",\"target\":1}";
  deliverEvent(reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), src, 1000,
               r.director, registry);
  TEST_ASSERT_EQUAL_INT(1, spy.calls);
}

void test_a_suppressor_mid_moment_ends_it() {
  Rig r;
  r.director.force(MomentKind::Fireflies, 99, 0);
  r.run(0, 5000, 5);
  const size_t before = r.host.renders.size();
  r.host.suppress = "wisp";
  r.run(5005, 20000, 5);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT32(before, r.host.renders.size());
}

void test_unsent_fireflies_start_aborts_but_still_grows() {
  Rig r;
  r.host.sendable = false;
  r.director.force(MomentKind::Fireflies, 99, 0);
  TEST_ASSERT_TRUE(r.director.active());
  r.run(0, 1500, 10);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT8(1, r.director.growth(MomentKind::Fireflies));
  TEST_ASSERT_TRUE(r.host.sent.empty());
}

void test_frames_go_out_twice_with_a_fresh_age() {
  Rig r;
  r.director.force(MomentKind::Fireflies, 99, 0);
  r.run(0, 1000, 10);
  TEST_ASSERT_EQUAL_UINT32(2, r.host.sent.size());
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"moment\",\"mo\":{\"k\":1,\"s\":99,\"a\":0}}", r.host.sent[0].c_str());
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"moment\",\"mo\":{\"k\":1,\"s\":99,\"a\":200}}", r.host.sent[1].c_str());
}


void test_a_busy_lamp_refuses_a_whisper() {
  Rig r(9);
  r.director.force(MomentKind::Fireflies, 1, 0);
  uint8_t sender[6];
  macOf(8, sender);
  r.frame(sender, whisperTo(r.self, 5, {8}), 1000);
  TEST_ASSERT_TRUE(r.director.activeKind() == MomentKind::Fireflies);
  TEST_ASSERT_EQUAL_UINT8(0, r.director.growth(MomentKind::Whisper));
}

void test_an_accepter_glows_one_hand_off_after_its_sender() {
  Rig r(9);
  uint8_t sender[6];
  macOf(8, sender);
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 5;
  f.ageMs = 500;
  f.hop = 1;
  macOf(8, f.visited[0]);
  std::memcpy(f.to, r.self, 6);
  r.frame(sender, encode(f), 1000);
  r.run(1000, 6000, 10);
  TEST_ASSERT_EQUAL_UINT32(1, r.host.rendersOf("pulse"));
  TEST_ASSERT_EQUAL_UINT32(1000 - 500 + MomentDirector::kWhisperHandOffMs, r.host.renders[0].atMs);
}

void test_whisper_start_thresholds() {
  std::map<uint64_t, uint8_t> d;
  auto disp = [&](const uint8_t* m) { return d[macKey(m)]; };
  const uint32_t now = 50000;
  struct Case {
    uint8_t disposition;
    int8_t rssi;
    uint32_t seenMs;
    bool starts;
  } cases[] = {
      {kDispositionSmitten, -70, now, true},  {kDispositionSmitten, -71, now, false},
      {kDispositionFond, -62, now, true},     {kDispositionFond, -63, now, false},
      {kDispositionNeutral, -30, now, false}, {kDispositionSmitten, -40, now - 15001, false},
      // Stamped by the BLE task just after the loop read the clock.
      {kDispositionSmitten, -40, now + 5, true},
  };
  for (const Case& c : cases) {
    std::vector<NearbyCopy> peers = {peerOf(1, c.rssi, c.seenMs)};
    d[macKey(peers[0].mac)] = c.disposition;
    TEST_ASSERT_EQUAL(c.starts, hasWhisperTarget(peers, now, disp));
  }
  // A direct HELLO keeps the peer near while its BLE RSSI goes stale.
  std::vector<NearbyCopy> peers = {peerOf(1, -40, now - 20000)};
  peers[0].lastSeenNearMs = now;
  d[macKey(peers[0].mac)] = kDispositionSmitten;
  TEST_ASSERT_FALSE(hasWhisperTarget(peers, now, disp));
}

void test_whisper_starts_naturally_and_announces_at_once() {
  Rig r(1, 9, 0);
  r.host.like(30, kDispositionFond);
  r.host.peers.push_back(peerOf(30, -55, 0));
  runFresh(r, 0, 30 * kMin, 10);
  bool started = false;
  for (const std::string& l : r.host.logs) started = started || l.rfind("[moment] start k=2", 0) == 0;
  TEST_ASSERT_TRUE(started);
  TEST_ASSERT_TRUE(r.host.sent.size() >= 2);
  const MomentFrame f = decodeSent(r.host.sent[0]);
  TEST_ASSERT_EQUAL_UINT8(1, f.hop);
  TEST_ASSERT_EQUAL_UINT16(0, f.ageMs);
  TEST_ASSERT_EQUAL_UINT8(30, f.to[5]);
  TEST_ASSERT_EQUAL_MEMORY(r.self, f.visited[0], 6);
  // The first hop leaves with the origin's glow.
  TEST_ASSERT_EQUAL_UINT32(r.host.renders[0].atMs, r.host.sentAt[0]);
}

void test_fireflies_needs_three_liked_peers() {
  for (uint8_t count : {2, 3}) {
    Rig r(1, 9, 0);
    addLikedPeers(r, 0, count);
    r.director.begin(&r.host, r.self, r.seed, 0);
    runFresh(r, 0, 100 * kMin, 1000);
    runFresh(r, 100 * kMin, 100 * kMin + 100000, 100);
    TEST_ASSERT_EQUAL(count == 3, r.host.rendersOf("flicker") > 0);
  }
}

void test_hop_floor_is_minus_eighty() {
  std::map<uint64_t, uint8_t> d;
  auto disp = [&](const uint8_t* m) { return d[macKey(m)]; };
  uint8_t self[6], out[6];
  macOf(99, self);
  std::vector<NearbyCopy> peers = {peerOf(1, -80, 1000)};
  d[macKey(peers[0].mac)] = kDispositionFond;
  TEST_ASSERT_TRUE(selectWhisperHop(peers, 1000, self, nullptr, 0, disp, out));
  peers[0].lastRssi = -81;
  TEST_ASSERT_FALSE(selectWhisperHop(peers, 1000, self, nullptr, 0, disp, out));
}

void test_a_greeting_does_not_block_joining_or_accepting() {
  uint8_t origin[6];
  macOf(9, origin);
  Rig ff;
  ff.host.greeting = true;
  ff.frame(origin, firefliesStart(5), 1000);
  TEST_ASSERT_TRUE(ff.director.active());
  Rig wh(9);
  wh.host.greeting = true;
  uint8_t sender[6];
  macOf(8, sender);
  wh.frame(sender, whisperTo(wh.self, 5, {8}), 1000);
  TEST_ASSERT_TRUE(wh.director.active());
}

// A backoff that expires between two crowd evaluations re-checks before it
// starts anything: a greeting that began after it was armed holds it.
void test_backoff_expiry_rechecks_before_starting() {
  bool exercised = false;
  for (uint32_t seed = 1; seed < 2000 && !exercised; ++seed) {
    Rig r(1, seed, 0);
    const uint32_t t = 100 * kMin;
    runFresh(r, 0, t - 1000, 1000);  // eligible, nobody near yet
    r.host.like(30, kDispositionFond);
    r.host.peers.push_back(peerOf(30, -55, t));
    r.host.nowMs = t;
    r.director.tick(t);
    if (!r.director.backoffArmed(MomentKind::Whisper)) continue;
    r.host.greeting = true;
    for (uint32_t now = t + 1; now < t + MomentDirector::kEvalPeriodMs; ++now) {
      for (NearbyCopy& p : r.host.peers) seeAt(p, now);
      r.director.tick(now);
      if (!r.director.backoffArmed(MomentKind::Whisper)) {
        exercised = true;
        break;
      }
    }
    if (exercised) {
      TEST_ASSERT_FALSE(r.director.active());
      TEST_ASSERT_EQUAL_UINT32(0, r.host.renders.size());
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(exercised, "no seed expired between evaluations");
}

void test_encode_refuses_a_short_buffer() {
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 1;
  f.hop = 8;
  char big[256];
  const size_t n = encodeMomentFrame(f, big, sizeof(big));
  TEST_ASSERT_TRUE(n > 0);
  char exact[256];
  TEST_ASSERT_EQUAL_UINT32(n, encodeMomentFrame(f, exact, n));
  std::memset(exact, '#', sizeof(exact));
  TEST_ASSERT_EQUAL_UINT32(0, encodeMomentFrame(f, exact, n - 1));
  TEST_ASSERT_EQUAL_CHAR('#', exact[0]);
}

void test_encode_clamps_the_hop_count_with_the_list() {
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 1;
  f.hop = kWhisperMaxHops + 1;
  const MomentFrame back = decodeSent(encode(f));
  TEST_ASSERT_EQUAL_UINT8(kWhisperMaxHops, back.hop);
}

void test_with_no_lit_colour_a_moment_renders_white() {
  Rig r;
  r.host.shade = {Color(3, 2, 1, 0), Color(0, 0, 0, 0)};
  r.director.force(MomentKind::Fireflies, 1, 0);
  r.run(0, 3000, 5);
  TEST_ASSERT_FALSE(r.host.renders.empty());
  const std::vector<Color>& c = r.host.renders.front().inv.colors;
  TEST_ASSERT_EQUAL_UINT32(1, c.size());
  TEST_ASSERT_TRUE(c[0] == Expression::kSafeFallbackColor);
}

void test_a_dark_whisper_origin_falls_back_to_the_shade() {
  Rig hop(2);
  hop.host.shade = {Color(0, 0, 170, 0)};
  MomentFrame f;
  f.kind = MomentKind::Whisper;
  f.seed = 5;
  f.hop = 1;
  macOf(1, f.visited[0]);
  std::memcpy(f.to, hop.self, 6);
  f.hasColor = true;
  f.color = Color(0, 0, 0, 0);
  uint8_t sender[6];
  macOf(1, sender);
  hop.frame(sender, encode(f), 0);
  hop.run(0, 4000, 10);
  TEST_ASSERT_EQUAL_UINT32(1, hop.host.rendersOf("pulse"));
  TEST_ASSERT_TRUE(hop.host.renders[0].inv.colors[0] == Color(0, 0, 170, 0));
}

void test_whitening_checks_every_base_stop() {
  Rig r;
  r.host.base = {Color(255, 0, 0, 0), Color(0, 250, 5, 0)};
  r.host.shade = {Color(0, 255, 0, 0)};
  r.director.force(MomentKind::Whisper, 3, 0);
  TEST_ASSERT_TRUE(r.host.renders.back().inv.colors[0] == bloomWhiten(Color(0, 255, 0, 0), 102));
}

void test_a_near_white_base_keeps_the_colour() {
  Rig r;
  r.host.base = {Color(250, 250, 250, 250)};
  r.host.shade = {Color(255, 255, 255, 230)};
  r.director.force(MomentKind::Whisper, 3, 0);
  TEST_ASSERT_TRUE(r.host.renders.back().inv.colors[0] == Color(255, 255, 255, 230));
}

void test_a_suppressor_cancels_a_pending_resend() {
  Rig r;
  r.host.like(30, kDispositionFond);
  r.host.peers.push_back(peerOf(30, -50, 0));
  r.director.force(MomentKind::Whisper, 3, 0);
  TEST_ASSERT_FALSE(r.director.active());
  TEST_ASSERT_EQUAL_UINT32(1, r.host.sent.size());
  r.run(0, 100, 10);
  r.host.suppress = "ota";
  r.run(110, 1500, 10);
  TEST_ASSERT_EQUAL_UINT32(1, r.host.sent.size());
}

void test_a_non_object_mo_is_swallowed() {
  Rig r;
  ExpressionObserverRegistry registry;
  MirrorSpy spy;
  registry.registerObserver(&spy);
  uint8_t src[6];
  macOf(9, src);
  const std::string odd = "{\"type\":\"pulse\",\"target\":1,\"mo\":5}";
  deliverEvent(reinterpret_cast<const uint8_t*>(odd.data()), odd.size(), src, 1000, r.director,
               registry);
  TEST_ASSERT_EQUAL_INT(0, spy.calls);
  TEST_ASSERT_FALSE(r.director.active());
}

void test_a_stamp_older_than_the_signed_horizon_is_stale() {
  const uint32_t now = 0x90000000u;
  NearbyCopy p = peerOf(1, -40, now - 25u * 24 * 3600 * 1000);
  TEST_ASSERT_FALSE(isFresh(p, now));
  p.lastSeenBleMs = now + 5;
  TEST_ASSERT_TRUE(isFresh(p, now));
  p.lastSeenBleMs = now - kMomentFreshMs;
  TEST_ASSERT_TRUE(isFresh(p, now));
  p.lastSeenBleMs = now - kMomentFreshMs - 1;
  TEST_ASSERT_FALSE(isFresh(p, now));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wait_lies_in_the_range_times_mode);
  RUN_TEST(test_growth_shifts_both_ends_and_stops_at_the_cap);
  RUN_TEST(test_participation_grows_and_rerolls_from_now);
  RUN_TEST(test_state_resets_on_begin);
  RUN_TEST(test_mode_change_does_not_reroll);
  RUN_TEST(test_hearing_a_frame_sets_the_crowd_cooldown_with_max);
  RUN_TEST(test_cooldown_blocks_initiation_but_not_joining);
  RUN_TEST(test_cooldown_does_not_block_accepting_a_whisper);
  RUN_TEST(test_initiates_after_eligibility_and_backoff);
  RUN_TEST(test_a_lapse_disarms_the_backoff);
  RUN_TEST(test_suppressors_block_initiation);
  RUN_TEST(test_herd_starts_one_fireflies_in_a_room_of_fifty);
  RUN_TEST(test_herd_starts_one_whisper_among_five);
  RUN_TEST(test_herd_starts_one_whisper_among_ten);
  RUN_TEST(test_herd_starts_one_whisper_among_twenty);
  RUN_TEST(test_fireflies_period_and_first_offset_ranges);
  RUN_TEST(test_fireflies_offset_shrinks_and_reaches_zero_by_84);
  RUN_TEST(test_fireflies_two_lamps_land_on_one_beat);
  RUN_TEST(test_fireflies_known_vectors);
  RUN_TEST(test_fireflies_envelope_endpoints);
  RUN_TEST(test_fireflies_timeline_is_deterministic);
  RUN_TEST(test_fireflies_late_joiner_recovers_t0_from_age);
  RUN_TEST(test_fireflies_join_gates);
  RUN_TEST(test_fireflies_resent_copy_is_ignored);
  RUN_TEST(test_liked_fingerprint_tracks_the_liked_set_only);
  RUN_TEST(test_ten_still_minutes_make_fireflies_possible);
  RUN_TEST(test_a_liked_departure_resets_the_still_clock);
  RUN_TEST(test_still_clock_stays_run_a_wrap_later);
  RUN_TEST(test_next_hop_picks_the_strongest_liked_fresh_unvisited);
  RUN_TEST(test_next_hop_ties_go_to_smitten_then_lower_mac);
  RUN_TEST(test_no_candidate_ends_the_chain);
  RUN_TEST(test_hop_eight_does_not_forward);
  RUN_TEST(test_malformed_whisper_frames_are_dropped);
  RUN_TEST(test_duplicate_whisper_is_ignored);
  RUN_TEST(test_wary_toward_the_sender_drops_the_whisper);
  RUN_TEST(test_whisper_suppressors);
  RUN_TEST(test_ring_of_ten_never_revisits_and_ends_within_eight_hops);
  RUN_TEST(test_hop_eight_payload_fits_the_event_frame);
  RUN_TEST(test_codec_round_trips);
  RUN_TEST(test_unknown_kind_is_ignored);
  RUN_TEST(test_older_lamps_parse_the_sentinel);
  RUN_TEST(test_renders_are_remote_terminal_and_on_the_base);
  RUN_TEST(test_fireflies_take_the_shade_colours);
  RUN_TEST(test_whisper_takes_the_origins_colour);
  RUN_TEST(test_a_colour_matching_the_base_is_whitened);
  RUN_TEST(test_a_malformed_origin_colour_drops_the_frame);
  RUN_TEST(test_moment_frames_never_reach_the_mirror);
  RUN_TEST(test_a_suppressor_mid_moment_ends_it);
  RUN_TEST(test_unsent_fireflies_start_aborts_but_still_grows);
  RUN_TEST(test_frames_go_out_twice_with_a_fresh_age);
  RUN_TEST(test_a_busy_lamp_refuses_a_whisper);
  RUN_TEST(test_an_accepter_glows_one_hand_off_after_its_sender);
  RUN_TEST(test_whisper_start_thresholds);
  RUN_TEST(test_whisper_starts_naturally_and_announces_at_once);
  RUN_TEST(test_fireflies_needs_three_liked_peers);
  RUN_TEST(test_hop_floor_is_minus_eighty);
  RUN_TEST(test_a_greeting_does_not_block_joining_or_accepting);
  RUN_TEST(test_backoff_expiry_rechecks_before_starting);
  RUN_TEST(test_encode_refuses_a_short_buffer);
  RUN_TEST(test_encode_clamps_the_hop_count_with_the_list);
  RUN_TEST(test_with_no_lit_colour_a_moment_renders_white);
  RUN_TEST(test_a_dark_whisper_origin_falls_back_to_the_shade);
  RUN_TEST(test_whitening_checks_every_base_stop);
  RUN_TEST(test_a_near_white_base_keeps_the_colour);
  RUN_TEST(test_a_suppressor_cancels_a_pending_resend);
  RUN_TEST(test_a_non_object_mo_is_swallowed);
  RUN_TEST(test_a_stamp_older_than_the_signed_horizon_is_stale);
  return UNITY_END();
}
