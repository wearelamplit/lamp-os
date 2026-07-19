// Native-host tests for the staged snafu greeting sequence.
//
// Pins the three-tier, two-stage social greeting behaviour: stages fire on
// elapsed time and SocialMode alone, gated only by the greeted peer having a
// MAC. Mirrors the production logic inline (no Arduino / FreeRTOS) so the
// tests compile on native. The production SocialMode enum and the
// LampRoster queries it uses are reproduced verbatim; keep them in sync
// with config_types.hpp and lamp_roster.hpp.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>

// ---- Time control -------------------------------------------------------

static uint32_t s_nowMs = 10000;
static uint32_t millis() { return s_nowMs; }

// ---- Minimal type mirrors -----------------------------------------------

namespace lamp {

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && w == o.w;
  }
};

enum class SocialMode : uint8_t { Introvert = 0, Ambivert = 1, Extrovert = 2 };

static constexpr uint8_t kTargetShade = 1;
static constexpr uint32_t kFastGlitchFrames = 12;

struct ExpressionInvocation {
  std::string type;
  std::vector<Color> colors;
  uint8_t target = 3;
  std::map<std::string, uint32_t> parameters;
  uint32_t delayMs = 0;
};

struct RosterEntry {
  std::string name;
  std::string lampId;
  Color baseColor;
  uint8_t mac[6] = {0};
  bool hasMac = false;
  uint32_t lastSeenNearMs = 0;
  uint32_t lastSeenMeshMs = 0;
  bool acknowledged = false;
};

// Inline LampRoster mirror sufficient for the tests. No FreeRTOS locks.
class LampRoster {
 public:
  void seedBle(const std::string& name, const std::string& lampId,
               const Color& base, uint32_t lastSeenNearMs, bool ack) {
    RosterEntry e;
    e.name = name;
    e.lampId = lampId;
    e.baseColor = base;
    e.lastSeenNearMs = lastSeenNearMs;
    e.acknowledged = ack;
    store_.push_back(e);
  }

  // Seeds a peer visible on both transports.
  void seedMesh(const std::string& name, const std::string& lampId,
                const Color& base, const uint8_t mac[6],
                uint32_t lastSeenNearMs, uint32_t lastSeenMeshMs,
                bool ack) {
    RosterEntry e;
    e.name = name;
    e.lampId = lampId;
    e.baseColor = base;
    std::memcpy(e.mac, mac, 6);
    e.hasMac = true;
    e.lastSeenNearMs = lastSeenNearMs;
    e.lastSeenMeshMs = lastSeenMeshMs;
    e.acknowledged = ack;
    store_.push_back(e);
  }

  std::vector<RosterEntry> getUngreetedArrivals(uint32_t maxAgeMs) {
    uint32_t now = millis();
    std::vector<RosterEntry> out;
    for (const auto& e : store_) {
      if (e.lampId.empty()) continue;
      if (e.lastSeenNearMs == 0) continue;
      if ((now - e.lastSeenNearMs) > maxAgeMs) continue;
      if (e.acknowledged) continue;
      out.push_back(e);
    }
    return out;
  }

  void acknowledge(const std::string& name) {
    for (auto& e : store_) {
      if (e.name == name) { e.acknowledged = true; return; }
    }
  }

 private:
  std::vector<RosterEntry> store_;
};

// ---- Fake ExpressionManager ---------------------------------------------

struct SendRecord {
  // true = directed (sendInvocationTo), false = broadcast
  bool directed = false;
  uint8_t mac[6] = {0};           // meaningful only when directed
  ExpressionInvocation inv;
};

class FakeExpressionManager {
 public:
  std::vector<SendRecord> sent;

  void clear() { sent.clear(); }

  void sendInvocationTo(const uint8_t mac[6], const ExpressionInvocation& inv) {
    SendRecord r;
    r.directed = true;
    std::memcpy(r.mac, mac, 6);
    r.inv = inv;
    sent.push_back(r);
  }

  // excludeMac may be null.
  void broadcastInvocation(const ExpressionInvocation& inv,
                           const uint8_t* excludeMac = nullptr) {
    SendRecord r;
    r.directed = false;
    if (excludeMac) std::memcpy(r.mac, excludeMac, 6);
    r.inv = inv;
    sent.push_back(r);
  }
};

// ---- Inline mirror of Greeting staged logic -----------------------------
//
// Mirrors production greeting.cpp stage-fire logic so the tests compile
// on native without Arduino headers. Keep in sync with greeting.cpp.

static constexpr uint32_t kBleMaxAgeMs     = 5000;
static constexpr uint32_t kStage2Ms        = 700;
static constexpr uint32_t kStage3Ms        = 1400;

struct GreetState {
  bool     active      = false;
  std::string greetedName;
  std::string greetedLampId;
  uint8_t  greetedMac[6] = {0};
  bool     hasMac      = false;
  Color    peerColor;
  uint32_t greetStartMs = 0;
  bool     stage2Done  = false;
  bool     stage3Done  = false;
};

// doGreet: start a greeting sequence (stage 1 implicit in tests via state set).
static void doGreet(GreetState& g, const RosterEntry& peer,
                    LampRoster& lamps) {
  g.active         = true;
  g.greetedName    = peer.name;
  g.greetedLampId  = peer.lampId;
  std::memcpy(g.greetedMac, peer.mac, 6);
  g.hasMac         = peer.hasMac;
  g.peerColor      = peer.baseColor;
  g.greetStartMs   = millis();
  g.stage2Done     = false;
  g.stage3Done     = false;
  lamps.acknowledge(peer.name);
}

// tickStages: call each control() iteration after doGreet. Fires stage 2
// at kStage2Ms and stage 3 at kStage3Ms via the fake manager, gated only
// on elapsed time, SocialMode, and the greeted peer having a MAC.
static void tickStages(GreetState& g, SocialMode mode,
                       const Color& stemColor,
                       FakeExpressionManager& mgr) {
  if (!g.active) return;
  const uint32_t elapsed = millis() - g.greetStartMs;

  if (!g.stage2Done &&
      elapsed >= kStage2Ms &&
      (mode == SocialMode::Ambivert || mode == SocialMode::Extrovert)) {
    g.stage2Done = true;
    if (g.hasMac) {
      ExpressionInvocation inv;
      inv.type   = "glitchy";
      inv.colors = {g.peerColor};
      inv.target = kTargetShade;
      inv.parameters = {{"durationMin", kFastGlitchFrames},
                        {"durationMax", kFastGlitchFrames}};
      inv.delayMs = 0;
      mgr.sendInvocationTo(g.greetedMac, inv);
    }
  }

  if (!g.stage3Done &&
      elapsed >= kStage3Ms &&
      mode == SocialMode::Extrovert) {
    g.stage3Done = true;
    if (g.hasMac) {
      ExpressionInvocation inv;
      inv.type   = "glitchy";
      inv.colors = {stemColor, g.peerColor};
      inv.target = kTargetShade;
      inv.parameters = {{"durationMin", kFastGlitchFrames},
                        {"durationMax", kFastGlitchFrames}};
      inv.delayMs = 0;
      mgr.broadcastInvocation(inv, g.greetedMac);
    }
  }
}

}  // namespace lamp

// ---- Fixtures -----------------------------------------------------------

static const uint8_t kMacA[6] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05};
static const uint8_t kMacB[6] = {0xBB, 0x01, 0x02, 0x03, 0x04, 0x05};
static const lamp::Color kBlue  = {0x00, 0x00, 0xFF, 0x00};
static const lamp::Color kRed   = {0xFF, 0x00, 0x00, 0x00};
static const lamp::Color kStem  = {0x30, 0x07, 0x83, 0x00};

void setUp()    { s_nowMs = 10000; }
void tearDown() {}

// ---- Tests --------------------------------------------------------------

// Introvert: stage 1 only; zero mesh sends regardless of mesh reachability.
void test_introvert_no_mesh_sends() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, arrivals.size());

  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  // Jump past both stage thresholds.
  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Introvert, kStem, mgr);

  TEST_ASSERT_EQUAL_UINT(0, mgr.sent.size());
}

// Ambivert: exactly one directed send to the greeted mac; no broadcast.
void test_ambivert_directed_only() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);
  // Second mesh peer present (to confirm no crowd broadcast goes there).
  lamps.seedMesh("gramp", "AA:BB:CC:DD:EE:02", kRed, kMacB,
                 s_nowMs - 100, s_nowMs - 100, true);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, arrivals.size());

  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Ambivert, kStem, mgr);

  TEST_ASSERT_EQUAL_UINT(1, mgr.sent.size());
  TEST_ASSERT_TRUE(mgr.sent[0].directed);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, mgr.sent[0].mac, 6);
}

// Extrovert: directed to greeted mac AND a broadcast excluding greeted mac.
void test_extrovert_directed_and_broadcast() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);
  lamps.seedMesh("gramp", "AA:BB:CC:DD:EE:02", kRed, kMacB,
                 s_nowMs - 100, s_nowMs - 100, true);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, arrivals.size());

  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  // Exactly two sends: directed + broadcast.
  TEST_ASSERT_EQUAL_UINT(2, mgr.sent.size());

  // First send is directed to the greeted mac.
  const auto* dir = [&]() -> const lamp::SendRecord* {
    for (const auto& r : mgr.sent)
      if (r.directed) return &r;
    return nullptr;
  }();
  TEST_ASSERT_NOT_NULL(dir);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, dir->mac, 6);

  // Second send is broadcast; its excludeMac is the greeted mac.
  const auto* bcast = [&]() -> const lamp::SendRecord* {
    for (const auto& r : mgr.sent)
      if (!r.directed) return &r;
    return nullptr;
  }();
  TEST_ASSERT_NOT_NULL(bcast);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, bcast->mac, 6);  // stored as excludeMac
}

// Opportunistic: greeted peer has no mac (BLE-only); no stage 2/3 sends.
void test_extrovert_no_send_when_peer_has_no_mac() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  // BLE-only peer: no lastSeenMeshMs.
  lamps.seedBle("flora", "AA:BB:CC:DD:EE:01", kBlue, s_nowMs - 100, false);

  // Manually build a RosterEntry with hasMac=false (BLE-only).
  lamp::RosterEntry peer;
  peer.name             = "flora";
  peer.lampId           = "AA:BB:CC:DD:EE:01";
  peer.baseColor        = kBlue;
  peer.hasMac           = false;
  peer.lastSeenNearMs = s_nowMs - 100;

  lamp::GreetState g;
  lamp::doGreet(g, peer, lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  // No mesh sends because hasMac is false.
  TEST_ASSERT_EQUAL_UINT(0, mgr.sent.size());
}

// Two-color: Extrovert crowd invocation carries {stemColor, peer.baseColor}.
void test_extrovert_broadcast_two_color_palette() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  TEST_ASSERT_EQUAL_UINT(1, arrivals.size());

  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  const lamp::SendRecord* bcast = nullptr;
  for (const auto& r : mgr.sent)
    if (!r.directed) bcast = &r;

  TEST_ASSERT_NOT_NULL(bcast);
  TEST_ASSERT_EQUAL_UINT(2, bcast->inv.colors.size());
  TEST_ASSERT_TRUE(bcast->inv.colors[0] == kStem);
  TEST_ASSERT_TRUE(bcast->inv.colors[1] == kBlue);
}

// Stage 2 must not fire before kStage2Ms elapses.
void test_stage2_does_not_fire_before_threshold() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  // Advance to just before the threshold.
  s_nowMs += lamp::kStage2Ms - 1;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  TEST_ASSERT_EQUAL_UINT(0, mgr.sent.size());
}

// Stage 3 must not fire before kStage3Ms elapses.
void test_stage3_does_not_fire_before_threshold() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  // Advance past stage 2 but not stage 3.
  s_nowMs += lamp::kStage2Ms + 100;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);
  // Only stage 2 (directed) fired.
  TEST_ASSERT_EQUAL_UINT(1, mgr.sent.size());
  TEST_ASSERT_TRUE(mgr.sent[0].directed);
}

// Peer has a MAC but was seen only via BLE (never confirmed ESP-NOW
// reachable). Stages 2 and 3 still fire: elapsed time and SocialMode are
// the only preconditions.
void test_extrovert_sends_even_when_not_mesh_reachable() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamp::RosterEntry peer;
  peer.name             = "flora";
  peer.lampId           = "AA:BB:CC:DD:EE:01";
  peer.baseColor        = kBlue;
  std::memcpy(peer.mac, kMacA, 6);
  peer.hasMac           = true;
  peer.lastSeenNearMs = s_nowMs - 100;

  lamp::GreetState g;
  lamp::doGreet(g, peer, lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  TEST_ASSERT_EQUAL_UINT(2, mgr.sent.size());
}

// Stages fire at most once each.
void test_stages_fire_at_most_once() {
  lamp::LampRoster lamps;
  lamp::FakeExpressionManager mgr;

  lamps.seedMesh("flora", "AA:BB:CC:DD:EE:01", kBlue, kMacA,
                 s_nowMs - 100, s_nowMs - 100, false);

  auto arrivals = lamps.getUngreetedArrivals(5000);
  lamp::GreetState g;
  lamp::doGreet(g, arrivals[0], lamps);

  s_nowMs += 1500;
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);
  lamp::tickStages(g, lamp::SocialMode::Extrovert, kStem, mgr);

  // Each stage fires exactly once.
  TEST_ASSERT_EQUAL_UINT(2, mgr.sent.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_introvert_no_mesh_sends);
  RUN_TEST(test_ambivert_directed_only);
  RUN_TEST(test_extrovert_directed_and_broadcast);
  RUN_TEST(test_extrovert_no_send_when_peer_has_no_mac);
  RUN_TEST(test_extrovert_sends_even_when_not_mesh_reachable);
  RUN_TEST(test_extrovert_broadcast_two_color_palette);
  RUN_TEST(test_stage2_does_not_fire_before_threshold);
  RUN_TEST(test_stage3_does_not_fire_before_threshold);
  RUN_TEST(test_stages_fire_at_most_once);
  return UNITY_END();
}
