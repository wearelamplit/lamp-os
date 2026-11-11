// Native tests for FondnessEngine: each rule moves the score the way its mode
// says, the caps and notch limits hold, and nothing records while the engine
// is off or home mode is on. Real DispositionStore over an InMemoryConfigStore.

#define LAMP_DEBUG 1

#include <unity.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <new>

#include "config/config_store.hpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/core/fondness_engine.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"

static int g_allocations = 0;
void* operator new(size_t size) {
  ++g_allocations;
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace lamp;

namespace {

constexpr uint64_t kMin = 60000;
constexpr uint64_t kHour = 60 * kMin;

bool g_home = false;
bool homeActive() { return g_home; }

std::array<uint8_t, 6> peer(unsigned i) {
  return {0x30, 0x00, 0x00, static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8),
          static_cast<uint8_t>(i)};
}

// Score units across steps: disposition x 127 + score.
int level(const DispositionStore& ds, const std::array<uint8_t, 6>& m, uint64_t now) {
  const PeerRecord r = ds.peer(m.data(), now);
  return r.disp * 127 + r.score;
}

struct Rig {
  DispositionStore store{100};
  FondnessEngine engine{store};
  uint64_t now = 0;

  Rig() {
    engine.setHomeSource(homeActive);
    engine.tick(now);
  }
  // Ticks once a minute for `minutes`, marking `near` seen at each commit.
  void run(uint64_t minutes, const std::array<uint8_t, 6>* near = nullptr) {
    for (uint64_t i = 0; i < minutes; ++i) {
      now += kMin;
      if (engine.tick(now) && near) engine.seen(near->data(), now);
    }
  }
  // `n` events of `type` from `m` inside one minute, then the commit.
  void burst(const std::array<uint8_t, 6>& m, const char* type, int n, bool ownType,
             SocialMode mode) {
    for (int k = 0; k < n; ++k) engine.observeExpression(m.data(), type, ownType, mode, now);
    run(1);
  }
};

}  // namespace

void setUp(void) { g_home = false; }
void tearDown(void) {}

// Energy cools an introvert, warms an extrovert and leaves an ambivert flat.
void test_loud_peer_by_mode() {
  const auto m = peer(1);
  int moved[3];
  for (int mode = 0; mode < 3; ++mode) {
    Rig rig;
    for (int minute = 0; minute < 10; ++minute) {
      rig.burst(m, "glitchy", 4, false, static_cast<SocialMode>(mode));
    }
    moved[mode] = level(rig.store, m, rig.now) - 3 * 127;
  }
  TEST_ASSERT_LESS_THAN(0, moved[0]);
  TEST_ASSERT_EQUAL(0, moved[1]);
  TEST_ASSERT_GREATER_THAN(0, moved[2]);
}

// A heavier type moves the score further than a light one, and a zero-weight
// type not at all.
void test_type_weights() {
  const auto m = peer(1);
  int moved[3];
  const char* types[] = {"glitchy", "breathing", "yawn"};
  for (int i = 0; i < 3; ++i) {
    Rig rig;
    rig.engine.tuning.deltaCapPerMin = 127;
    rig.burst(m, types[i], 1, false, SocialMode::Extrovert);
    moved[i] = level(rig.store, m, rig.now) - 3 * 127;
  }
  TEST_ASSERT_GREATER_THAN(moved[1], moved[0]);
  TEST_ASSERT_GREATER_THAN(0, moved[1]);
  TEST_ASSERT_EQUAL(0, moved[2]);
}

// Overlap warms only for a type this lamp has enabled itself.
void test_overlap_only_own_types() {
  const auto m = peer(1);
  Rig own;
  own.burst(m, "pulse", 3, true, SocialMode::Ambivert);
  Rig other;
  other.burst(m, "pulse", 3, false, SocialMode::Ambivert);
  TEST_ASSERT_GREATER_THAN(0, level(own.store, m, own.now) - 3 * 127);
  TEST_ASSERT_EQUAL(0, level(other.store, m, other.now) - 3 * 127);
}

// A burst moves one minute's commit by deltaCapPerMin at most, and only
// eventsPerMin of it count.
void test_burst_commits_at_most_the_cap() {
  const auto m = peer(1);
  Rig rig;
  rig.burst(m, "glitchy", 500, true, SocialMode::Introvert);
  TEST_ASSERT_EQUAL(-rig.engine.tuning.deltaCapPerMin, level(rig.store, m, rig.now) - 3 * 127);

  Rig counted;
  counted.engine.tuning.deltaCapPerMin = 127;
  counted.burst(m, "glitchy", 500, false, SocialMode::Extrovert);
  const FondnessTuning& t = counted.engine.tuning;
  TEST_ASSERT_EQUAL(t.eventsPerMin * t.energy * t.typeWeight[0],
                    level(counted.store, m, counted.now) - 3 * 127);
}

// Similar and complementary hues warm; a clash and a grey do not. Only the
// reply to a greet counts.
void test_colour_relation() {
  const Color own(255, 0, 0, 0);
  struct Case {
    Color peer;
    bool warms;
  } cases[] = {
      {Color(255, 60, 0, 0), true},    // ~14 deg
      {Color(0, 255, 255, 0), true},   // 180 deg
      {Color(0, 255, 0, 0), false},    // 120 deg
      {Color(0, 0, 0, 255), false},    // white only
      {Color(200, 180, 180, 0), false},
  };
  const auto m = peer(1);
  for (const Case& c : cases) {
    Rig rig;
    rig.engine.tuning.meetEveryH = 255;
    rig.store.meet(m.data(), 0, rig.now);
    rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
    const int before = level(rig.store, m, rig.now);
    rig.engine.observeColour(m.data(), c.peer, own, rig.now);
    TEST_ASSERT_EQUAL(c.warms ? rig.engine.tuning.colour : 0,
                      level(rig.store, m, rig.now) - before);
  }
  Rig unsolicited;
  unsolicited.engine.observeColour(m.data(), Color(255, 10, 0, 0), own, 0);
  TEST_ASSERT_EQUAL(3 * 127, level(unsolicited.store, m, 0));
  // A reply from another lamp than the one greeted does not count either.
  Rig wrong;
  wrong.engine.observeGreeting(peer(2).data(), SocialMode::Ambivert, 0);
  wrong.engine.observeColour(m.data(), Color(255, 10, 0, 0), own, 0);
  TEST_ASSERT_EQUAL(3 * 127, level(wrong.store, m, 0));
}

// Daily meetings alone take a neutral household lamp to Fond and no further.
void test_daily_meetings_stop_at_fond() {
  const auto m = peer(1);
  Rig rig;
  for (int day = 1; day <= 30; ++day) {
    rig.run(24 * 60, &m);
    rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
  }
  TEST_ASSERT_EQUAL_UINT8(30, rig.store.peer(m.data(), rig.now).meets);
  TEST_ASSERT_EQUAL_UINT8(4, rig.store.peer(m.data(), rig.now).disp);
}

// From Salty, with room to climb, each week of daily meetings warms less than
// the week before.
void test_meeting_warmth_diminishes() {
  const auto m = peer(1);
  Rig rig;
  rig.store.set(m.data(), 1, 0);
  int weekStart = level(rig.store, m, rig.now);
  int lastGain = 1 << 30;
  for (int day = 1; day <= 28; ++day) {
    rig.run(24 * 60, &m);
    rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
    if (day % 7 != 0) continue;
    const int gain = level(rig.store, m, rig.now) - weekStart;
    TEST_ASSERT_GREATER_THAN(0, gain);
    TEST_ASSERT_LESS_THAN(lastGain, gain);
    lastGain = gain;
    weekStart = level(rig.store, m, rig.now);
  }
}

// Meetings closer than meetEveryH on-hours count once.
void test_meetings_need_the_gap() {
  const auto m = peer(1);
  Rig rig;
  for (int k = 0; k < 10; ++k) {
    rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
    rig.run(60, &m);
  }
  TEST_ASSERT_EQUAL_UINT8(1, rig.store.peer(m.data(), rig.now).meets);
  rig.run(60 * 3, &m);
  rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
  TEST_ASSERT_EQUAL_UINT8(2, rig.store.peer(m.data(), rig.now).meets);
}

// An introvert's first meeting warms twice what an ambivert's does.
void test_introvert_familiarity_doubles() {
  const auto m = peer(1);
  Rig intro;
  intro.engine.observeGreeting(m.data(), SocialMode::Introvert, 0);
  Rig ambi;
  ambi.engine.observeGreeting(m.data(), SocialMode::Ambivert, 0);
  const int i = level(intro.store, m, 0) - 3 * 127;
  const int a = level(ambi.store, m, 0) - 3 * 127;
  TEST_ASSERT_GREATER_THAN(0, a);
  TEST_ASSERT_EQUAL(2 * a, i);
}

// The score steps the disposition at +1 and restarts at 0.
void test_score_steps_and_resets() {
  const auto m = peer(1);
  Rig rig;
  rig.engine.tuning.deltaCapPerMin = 127;
  rig.engine.tuning.notchFloorMin = 0;
  int minutes = 0;
  while (rig.store.peer(m.data(), rig.now).disp == 3 && minutes < 100) {
    rig.burst(m, "glitchy", 4, false, SocialMode::Extrovert);
    ++minutes;
  }
  const PeerRecord r = rig.store.peer(m.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT8(4, r.disp);
  TEST_ASSERT_EQUAL_INT8(0, r.score);
  TEST_ASSERT_EQUAL(6, minutes);  // 24 a minute: 5 x 24 = 120, the 6th reaches 127
}

// One step per peer per notchFloorMin; the score waits one short of it.
void test_notch_floor_holds() {
  const auto m = peer(1);
  Rig rig;
  rig.engine.tuning.deltaCapPerMin = 127;
  while (rig.store.peer(m.data(), rig.now).disp == 3) {
    rig.burst(m, "glitchy", 4, false, SocialMode::Extrovert);
  }
  const uint64_t notchedAt = rig.now;
  while (rig.now - notchedAt < rig.engine.tuning.notchFloorMin * kMin - kMin) {
    rig.burst(m, "glitchy", 4, false, SocialMode::Extrovert);
    TEST_ASSERT_EQUAL_UINT8(4, rig.store.peer(m.data(), rig.now).disp);
  }
  TEST_ASSERT_EQUAL_INT8(126, rig.store.peer(m.data(), rig.now).score);
  rig.burst(m, "glitchy", 4, false, SocialMode::Extrovert);
  rig.burst(m, "glitchy", 4, false, SocialMode::Extrovert);
  TEST_ASSERT_EQUAL_UINT8(5, rig.store.peer(m.data(), rig.now).disp);
}

// At most notchesPerHour steps across all peers in any hour.
void test_hourly_notch_cap_holds() {
  Rig rig;
  rig.engine.tuning.deltaCapPerMin = 127;
  const int peers = 8;
  int steps = 0;
  for (int minute = 0; minute < 59; ++minute) {
    for (int p = 0; p < peers; ++p) {
      const auto m = peer(p);
      for (int k = 0; k < 4; ++k) {
        rig.engine.observeExpression(m.data(), "glitchy", false, SocialMode::Extrovert, rig.now);
      }
    }
    rig.run(1);
  }
  for (int p = 0; p < peers; ++p) steps += rig.store.peer(peer(p).data(), rig.now).disp - 3;
  TEST_ASSERT_EQUAL(rig.engine.tuning.notchesPerHour, steps);
}

// Absence decays the score toward 0 per tracked hour and never moves the
// disposition; a lamp in the near view keeps its score.
void test_absence_decays_toward_zero() {
  const auto m = peer(1);
  const auto near = peer(2);
  Rig rig;
  rig.store.set(m.data(), 4, 0);
  rig.store.set(near.data(), 4, 0);
  rig.store.nudge(m.data(), 100, 0, 0);
  rig.store.nudge(near.data(), 100, 0, 0);
  rig.run(3 * 60, &near);
  const PeerRecord r = rig.store.peer(m.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT8(4, r.disp);
  TEST_ASSERT_LESS_THAN(100, r.score);
  TEST_ASSERT_GREATER_THAN(0, r.score);
  TEST_ASSERT_EQUAL_INT8(100, rig.store.peer(near.data(), rig.now).score);
}

// In home mode no input reaches a row or a slot, and the row's clocks stand.
void test_home_mode_records_nothing() {
  const auto known = peer(1);
  Rig rig;
  rig.store.set(known.data(), 4, 0);
  rig.store.meet(known.data(), 0, 0);
  rig.run(30);
  g_home = true;
  rig.engine.tick(rig.now);
  const PeerRecord before = rig.store.peer(known.data(), rig.now);
  for (int k = 0; k < 1000; ++k) {
    const auto m = peer(k % 20);
    rig.engine.observeExpression(m.data(), "glitchy", true, SocialMode::Introvert, rig.now);
    rig.engine.observeGreeting(m.data(), SocialMode::Introvert, rig.now);
    rig.engine.observeColour(m.data(), Color(255, 0, 0, 0), Color(255, 0, 0, 0), rig.now);
    rig.engine.seen(m.data(), rig.now);
    if (k % 10 == 0) rig.run(10);
  }
  TEST_ASSERT_EQUAL(0, rig.engine.slotsInUse());
  TEST_ASSERT_EQUAL(1, rig.store.peerCount());
  const PeerRecord after = rig.store.peer(known.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT8(before.disp, after.disp);
  TEST_ASSERT_EQUAL_INT8(before.score, after.score);
  TEST_ASSERT_EQUAL_UINT8(before.meets, after.meets);
  TEST_ASSERT_EQUAL_UINT8(before.meetAgeH, after.meetAgeH);
  TEST_ASSERT_EQUAL_UINT16(before.unseenMin, after.unseenMin);
}

// Home time counts toward neither the unseen nor the meeting clock; both run
// on once home mode ends.
void test_clocks_pause_while_not_tracking() {
  const auto m = peer(1);
  Rig rig;
  rig.store.set(m.data(), 4, 0);
  rig.engine.observeGreeting(m.data(), SocialMode::Ambivert, rig.now);
  rig.run(90);
  const PeerRecord before = rig.store.peer(m.data(), rig.now);
  g_home = true;
  rig.run(10 * 60);
  g_home = false;
  rig.run(1);
  PeerRecord after = rig.store.peer(m.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT16(before.unseenMin + 1, after.unseenMin);
  TEST_ASSERT_EQUAL_UINT8(before.meetAgeH, after.meetAgeH);
  rig.run(60);
  after = rig.store.peer(m.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT8(before.meetAgeH + 1, after.meetAgeH);

  rig.engine.setEnabled(false, rig.now);
  const PeerRecord off = rig.store.peer(m.data(), rig.now);
  rig.run(10 * 60);
  TEST_ASSERT_EQUAL_UINT16(off.unseenMin, rig.store.peer(m.data(), rig.now).unseenMin);
  TEST_ASSERT_EQUAL_UINT8(off.meetAgeH, rig.store.peer(m.data(), rig.now).meetAgeH);
}

// With the master switch off every input is a no-op, and the switch survives
// a reload.
void test_master_off_changes_nothing() {
  InMemoryConfigStore backing;
  const auto m = peer(1);
  Rig rig;
  rig.store.attachStore(&backing);
  rig.store.set(m.data(), 2, 0);
  rig.engine.setEnabled(false, 0);
  TEST_ASSERT_FALSE(rig.engine.tracking());
  for (int k = 0; k < 100; ++k) {
    rig.engine.observeExpression(peer(k).data(), "glitchy", true, SocialMode::Extrovert, rig.now);
    rig.engine.observeGreeting(peer(k).data(), SocialMode::Extrovert, rig.now);
    rig.run(30);
  }
  TEST_ASSERT_EQUAL(1, rig.store.peerCount());
  const PeerRecord r = rig.store.peer(m.data(), rig.now);
  TEST_ASSERT_EQUAL_UINT8(2, r.disp);
  TEST_ASSERT_EQUAL_INT8(0, r.score);
  TEST_ASSERT_EQUAL_UINT8(0, r.meets);
  rig.store.flushNow(rig.now);

  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load(0);
  FondnessEngine engine(reloaded);
  TEST_ASSERT_FALSE(engine.tracking());
}

// A crowd of 500 peers holds 16 slots and allocates nothing.
void test_crowd_stays_bounded() {
  Rig rig;
  rig.run(1);
  const int before = g_allocations;
  for (int k = 0; k < 10000; ++k) {
    const auto m = peer(k % 500);
    rig.engine.observeExpression(m.data(), k % 2 ? "glitchy" : "pulse", k % 3 == 0,
                                 static_cast<SocialMode>(k % 3), rig.now);
    if (k % 50 == 0) rig.engine.observeGreeting(m.data(), SocialMode::Introvert, rig.now);
    if (k % 100 == 0) rig.run(1);
    TEST_ASSERT_TRUE(rig.engine.slotsInUse() <= FondnessEngine::kSlots);
  }
  rig.run(24 * 60);
  TEST_ASSERT_EQUAL(0, g_allocations - before);
  TEST_ASSERT_TRUE(rig.store.peerCount() <= DispositionStore::kMax);
}

// fond.set takes known keys in range and leaves the table alone otherwise.
void test_tuning_keys_are_checked() {
  FondnessTuning t;
  TEST_ASSERT_TRUE(setFondnessTuning(t, "deltaCapPerMin", 60));
  TEST_ASSERT_EQUAL_UINT8(60, t.deltaCapPerMin);
  TEST_ASSERT_TRUE(setFondnessTuning(t, "notchFloorMin", 1));
  TEST_ASSERT_EQUAL_UINT16(1, t.notchFloorMin);
  TEST_ASSERT_TRUE(setFondnessTuning(t, "meetEveryH", 0));
  TEST_ASSERT_EQUAL_UINT8(0, t.meetEveryH);
  TEST_ASSERT_TRUE(setFondnessTuning(t, "e.shimmer", 5));
  TEST_ASSERT_EQUAL_UINT8(5, t.typeWeight[3]);
  TEST_ASSERT_TRUE(setFondnessTuning(t, "familiarMode.introvert", 3));
  TEST_ASSERT_EQUAL_UINT8(3, t.familiarMode[0]);

  const FondnessTuning snapshot = t;
  const struct {
    const char* key;
    uint32_t value;
  } bad[] = {{"nope", 1},           {"e.nope", 1},           {"e.", 1},
             {"deltaCapPerMin", 0}, {"deltaCapPerMin", 128}, {"notchFloorMin", 1441},
             {"notchesPerHour", 17}, {"energy", 33},         {"e.glitchy", 17},
             {"familiarMode.introvert", 5}, {"familiarMode", 1}, {"hueBand", 91}};
  for (const auto& b : bad) TEST_ASSERT_FALSE(setFondnessTuning(t, b.key, b.value));
  TEST_ASSERT_EQUAL_MEMORY(&snapshot, &t, sizeof(t));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_loud_peer_by_mode);
  RUN_TEST(test_type_weights);
  RUN_TEST(test_overlap_only_own_types);
  RUN_TEST(test_burst_commits_at_most_the_cap);
  RUN_TEST(test_colour_relation);
  RUN_TEST(test_daily_meetings_stop_at_fond);
  RUN_TEST(test_meeting_warmth_diminishes);
  RUN_TEST(test_meetings_need_the_gap);
  RUN_TEST(test_introvert_familiarity_doubles);
  RUN_TEST(test_score_steps_and_resets);
  RUN_TEST(test_notch_floor_holds);
  RUN_TEST(test_hourly_notch_cap_holds);
  RUN_TEST(test_absence_decays_toward_zero);
  RUN_TEST(test_home_mode_records_nothing);
  RUN_TEST(test_clocks_pause_while_not_tracking);
  RUN_TEST(test_master_off_changes_nothing);
  RUN_TEST(test_crowd_stays_bounded);
  RUN_TEST(test_tuning_keys_are_checked);
  return UNITY_END();
}
