// Native tests for the two framework-wired Lions behaviors: the always-PLAYING
// LionsAmbientBehavior (base scene + Greetable) and the STOPPED-until-arrival
// LionsGreetingBehavior. The pure units they compose (LionsDirector,
// renderLions, pulseEnvelope, evenZones) are tested separately; this exercises
// the glue that isn't pure:
//   1. Ambient draw() writes only the Lions segment slice, never Main.
//   2. Ambient control->director->cache->render composition: a cached peer's
//      stops fill every zone when that peer is the only one nearby.
//   3. Greetable delegation: ambient.triggerGreeting / greetingState route to
//      the greeting object; ambient owns the slot, greeting owns the pulse.
//   4. Latest-wins arrival: a newer arrival mid-pulse restarts the pulse.
//   5. Greeting draw() writes only the Lions slice and snaps it to the peer
//      colour at the pulse peak.
//   6. Ambient control() + draw() allocate nothing per frame once the stops
//      are settled, crossfade included.
//   7. Colour queries are throttled: a silent peer at most once per
//      kColorQueryRetryMs, at most three outstanding per window, and a shown
//      peer's cached stops survive later answers.
//
// Adafruit_NeoPixel + esp_system are stubbed under test/native_stubs/.
// forEachNearby / forEachArrival and the two mesh/expression sinks the
// behaviors reference are stubbed here so control() runs without the full
// roster / mesh stack.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/lamps/lioness/lions_ambient_behavior.cpp"
#include "../../src/lamps/lioness/lions_greeting_behavior.cpp"
#include "../../src/lamps/lioness/lions_scene.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../replaced_new/replaced_new.hpp"

static int g_allocations = 0;
void* testAllocate(size_t size) {
  ++g_allocations;
  return std::malloc(size);
}
void testRelease(void* p) noexcept { std::free(p); }

using namespace lamp;
using lamp::lioness::LionsAmbientBehavior;
using lamp::lioness::LionsGreetingBehavior;

namespace {

struct FakePeer {
  uint8_t mac[6];
  Color base;
  std::string lampId;
};

std::vector<FakePeer> g_nearby;
std::vector<FakePeer> g_arrivals;   // forEachArrival pops the front on ack

struct SentQuery { uint8_t tag; uint32_t ms; };
SentQuery g_queries[4096];
size_t g_queryCount = 0;

PeerView viewOf(const FakePeer& fp) {
  PeerView pv;
  pv.mac = fp.mac;
  pv.hasMac = true;
  pv.baseColor = fp.base;
  std::strncpy(pv.lampId, fp.lampId.c_str(), sizeof(pv.lampId) - 1);
  return pv;
}

FakePeer makePeer(uint8_t tag, Color base, const char* id) {
  FakePeer fp;
  std::memset(fp.mac, tag, 6);
  fp.base = base;
  fp.lampId = id;
  return fp;
}

}  // namespace

// Test-controlled roster/arrival seams. Definitions live here, not in
// behavior_context_mesh.cpp (which isn't linked into the native suite).
namespace lamp {
void BehaviorContext::forEachNearby(const std::function<bool(const PeerView&)>& cb) {
  for (const FakePeer& fp : g_nearby)
    if (cb(viewOf(fp))) break;
}
void BehaviorContext::forEachArrival(uint32_t,
                                     const std::function<bool(const PeerView&)>& cb) {
  if (g_arrivals.empty()) return;
  if (cb(viewOf(g_arrivals.front()))) g_arrivals.erase(g_arrivals.begin());
}
bool ExpressionManager::triggerInvocation(const ExpressionInvocation&,
                                          const uint8_t*, bool,
                                          TransientSource) { return false; }
bool MeshLink::sendColorQuery(const uint8_t* mac) {
  if (g_queryCount < sizeof g_queries / sizeof g_queries[0])
    g_queries[g_queryCount++] = {mac[0], millis()};
  return true;
}
}  // namespace lamp

static Adafruit_NeoPixel neoMain;
static Adafruit_NeoPixel neoLions;
static MeshLink g_link;

void setUp() {
  g_nearby.clear();
  g_arrivals.clear();
  g_queryCount = 0;
  set_mock_millis(0);
}
void tearDown() {}

// Base buffer: Main (offset 0, 32px) + Lions (offset 32, 18px). Matches the
// lioness variant map. Manual segment fill avoids FrameBuffer::begin's driver
// init.
static FrameBuffer makeFb() {
  FrameBuffer fb;
  fb.pixelCount = 50;
  fb.buffer.assign(50, Color(0, 0, 0, 0));
  fb.segments = {
    {&neoMain,  "Main",  0,  32, false},
    {&neoLions, "Lions", 32, 18, false},
  };
  return fb;
}

// Idle ambient scene mirrors Main into the Lions slice and never touches Main.
void test_ambient_draw_owns_only_lions_slice() {
  FrameBuffer fb = makeFb();
  const Color mainSentinel(0x11, 0x22, 0x33, 0x44);
  const Color lionsSentinel(0x99, 0x88, 0x77, 0x66);
  for (uint16_t i = 0; i < 32; ++i) fb.buffer[i] = mainSentinel;
  for (uint16_t i = 32; i < 50; ++i) fb.buffer[i] = lionsSentinel;

  LionsAmbientBehavior ambient(&fb);
  ambient.draw();   // no peers cached -> every zone idle -> mirror Main

  for (uint16_t i = 0; i < 32; ++i)
    TEST_ASSERT_TRUE(fb.buffer[i] == mainSentinel);          // Main untouched
  for (uint16_t i = 32; i < 50; ++i)
    TEST_ASSERT_TRUE(fb.buffer[i] == mainSentinel);          // Lions now mirrors Main
}

// One cached peer nearby -> director assigns all three lions to it -> the
// crossfade settles every Lions pixel onto that peer's colour.
void test_ambient_composes_cached_peer_colour() {
  FrameBuffer fb = makeFb();
  const Color mainColor(0x20, 0x08, 0x80, 0);
  for (uint16_t i = 0; i < 32; ++i) fb.buffer[i] = mainColor;

  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);

  const Color peerRed(0xF0, 0, 0, 0);
  FakePeer a = makePeer(0xA1, peerRed, "A1:A1:A1:A1:A1:A1");
  const uint8_t infoMac[6] = {0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1};
  ambient.onColorInfo(infoMac, {peerRed}, {});
  g_nearby = {a};

  ambient.control();                              // director -> all lions = A1
  for (uint32_t f = 0; f <= lamp::lioness::kCrossfadeFrames; ++f)
    ambient.draw();                               // let the peer-switch crossfade finish

  for (uint16_t i = 32; i < 50; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0xF0, fb.buffer[i].r);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].g);
    TEST_ASSERT_EQUAL_UINT8(0, fb.buffer[i].b);
  }
}

// The ambient Greetable delegates trigger + state to the greeting object.
void test_ambient_delegates_greeting_to_greeting_object() {
  FrameBuffer fb = makeFb();
  LionsAmbientBehavior ambient(&fb);

  TEST_ASSERT_FALSE(ambient.greetingState().active);   // no greeting wired yet

  LionsGreetingBehavior greeting(&fb);
  ambient.setGreeting(&greeting);
  TEST_ASSERT_FALSE(ambient.greetingState().active);   // greeting STOPPED

  FakePeer a = makePeer(0x0A, Color(0, 0xF0, 0, 0), "0A:0A:0A:0A:0A:0A");
  ambient.triggerGreeting(viewOf(a));                  // routes to greeting.startGreeting

  GreetingState gs = ambient.greetingState();
  TEST_ASSERT_TRUE(gs.active);
  TEST_ASSERT_EQUAL_STRING("0A:0A:0A:0A:0A:0A", gs.peerLampId.c_str());
  TEST_ASSERT_EQUAL_STRING("pulse", gs.kind.c_str());
  TEST_ASSERT_TRUE(greeting.greetingState().active);   // same underlying state
}

// A newer arrival mid-pulse restarts the pulse (frame back to 0, colour swaps).
void test_greeting_latest_wins_restarts_pulse() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsGreetingBehavior greeting(&fb);
  greeting.setBehaviorContext(&ctx);

  FakePeer a = makePeer(0x0A, Color(0xF0, 0, 0, 0), "0A:0A:0A:0A:0A:0A");
  g_arrivals = {a};
  greeting.control();                          // greet A
  TEST_ASSERT_EQUAL_UINT32(0, greeting.frame);
  TEST_ASSERT_EQUAL_STRING("0A:0A:0A:0A:0A:0A",
                           greeting.greetingState().peerLampId.c_str());

  for (int f = 0; f < 5; ++f) greeting.draw();  // advance the playhead
  TEST_ASSERT_TRUE(greeting.frame > 0);

  FakePeer b = makePeer(0x0B, Color(0, 0, 0xF0, 0), "0B:0B:0B:0B:0B:0B");
  g_arrivals = {b};
  greeting.control();                          // newer arrival restarts
  TEST_ASSERT_EQUAL_UINT32(0, greeting.frame);
  TEST_ASSERT_EQUAL_STRING("0B:0B:0B:0B:0B:0B",
                           greeting.greetingState().peerLampId.c_str());
}

// Greeting draw() paints only the Lions slice and snaps it to the peer colour
// at the pulse peak.
void test_greeting_draw_owns_only_lions_slice() {
  FrameBuffer fb = makeFb();
  const Color mainSentinel(0x11, 0x22, 0x33, 0x44);
  for (uint16_t i = 0; i < 32; ++i) fb.buffer[i] = mainSentinel;

  LionsGreetingBehavior greeting(&fb);
  FakePeer a = makePeer(0x0A, Color(0xF0, 0, 0, 0), "0A:0A:0A:0A:0A:0A");
  greeting.startGreeting(viewOf(a));
  greeting.frame = lamp::lioness::kFramesPerPulse / 2;   // pulseEnvelope peak -> bri 255
  greeting.draw();

  for (uint16_t i = 0; i < 32; ++i)
    TEST_ASSERT_TRUE(fb.buffer[i] == mainSentinel);   // Main untouched
  for (uint16_t i = 32; i < 50; ++i)
    TEST_ASSERT_EQUAL_UINT8(0xF0, fb.buffer[i].r);    // Lions = peer colour at peak
}

// The compositor stacks the ambient base behavior before the greeting, so a
// greeting frame composites over the ambient scene. Drawn ambient-first the
// greeting is visible; the reverse order lets the ambient clobber it. Same
// inputs, only the draw order differs -> the ordering is what makes the
// greeting show.
void test_ambient_draws_before_greeting_composites() {
  const Color mainColor(0x20, 0x08, 0x80, 0);
  const Color peerRed(0xF0, 0, 0, 0);
  FakePeer a = makePeer(0x0A, peerRed, "0A:0A:0A:0A:0A:0A");

  FrameBuffer fb = makeFb();
  for (uint16_t i = 0; i < 32; ++i) fb.buffer[i] = mainColor;
  LionsAmbientBehavior ambient(&fb);
  LionsGreetingBehavior greeting(&fb);
  greeting.startGreeting(viewOf(a));
  greeting.frame = lamp::lioness::kFramesPerPulse / 2;   // pulse peak -> bri 255

  ambient.draw();                                        // base scene first
  greeting.draw();                                       // pulse over it
  for (uint16_t i = 32; i < 50; ++i)
    TEST_ASSERT_EQUAL_UINT8(0xF0, fb.buffer[i].r);       // greeting visible

  FrameBuffer fb2 = makeFb();
  for (uint16_t i = 0; i < 32; ++i) fb2.buffer[i] = mainColor;
  LionsAmbientBehavior ambient2(&fb2);
  LionsGreetingBehavior greeting2(&fb2);
  greeting2.startGreeting(viewOf(a));
  greeting2.frame = lamp::lioness::kFramesPerPulse / 2;

  greeting2.draw();                                      // wrong order
  ambient2.draw();                                       // ambient clobbers
  for (uint16_t i = 32; i < 50; ++i)
    TEST_ASSERT_EQUAL_UINT8(0x20, fb2.buffer[i].r);      // greeting lost
}

static std::vector<Color> twoStops(uint8_t tag) {
  return {Color(tag, 0, 0xFF, 0), Color(0, tag, 0, 0xFF)};
}

static void answer(LionsAmbientBehavior& ambient, uint8_t tag) {
  uint8_t mac[6];
  std::memset(mac, tag, 6);
  ambient.onColorInfo(mac, twoStops(tag), {});
}

// Three cached multi-stop peers: once the stops settle, control() + draw()
// allocate nothing, through the peer-switch crossfade and after it.
void test_ambient_steady_state_allocates_nothing() {
  FrameBuffer fb = makeFb();
  for (uint16_t i = 0; i < 32; ++i) fb.buffer[i] = Color(0x20, 0x08, 0x80, 0);
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);

  const uint8_t tags[3] = {0xA1, 0xB2, 0xC3};
  for (uint8_t t : tags) {
    g_nearby.push_back(makePeer(t, Color(t, 0, 0, 0), "peer"));
    answer(ambient, t);
  }

  ambient.control();   // seeds the three lions, starts their crossfades
  ambient.draw();

  const int before = g_allocations;
  for (uint32_t f = 0; f < 2 * lamp::lioness::kCrossfadeFrames; ++f) {
    ambient.control();
    ambient.draw();
  }
  TEST_ASSERT_EQUAL_INT(0, g_allocations - before);
  TEST_ASSERT_EQUAL_size_t(0, g_queryCount);

  for (int z = 0; z < 3; ++z) {   // every zone shows a gradient, not a solid
    const Color first = fb.buffer[32 + z * 6];
    const Color last = fb.buffer[32 + z * 6 + 5];
    TEST_ASSERT_FALSE(first == last);
    TEST_ASSERT_EQUAL_UINT8(tags[z], first.r);
  }
}

// One silent peer shown by all three lions, polled every frame for several
// windows: never more than one query per kColorQueryRetryMs, and it still
// re-asks once each window has passed.
void test_silent_peer_queried_once_per_window() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);
  g_nearby = {makePeer(0x5E, Color(0x5E, 0, 0, 0), "silent")};

  const uint32_t window = lamp::lioness::kColorQueryRetryMs;
  const uint32_t span = 4 * window + window / 2;
  for (uint32_t t = 0; t <= span; t += 7) {
    set_mock_millis(t);
    ambient.control();
  }

  TEST_ASSERT_TRUE(g_queryCount >= 4);
  for (size_t i = 1; i < g_queryCount; ++i)
    TEST_ASSERT_TRUE(g_queries[i].ms - g_queries[i - 1].ms >= window);
}

// A crowd of silent peers rotating through the lions: within any window, at
// most three queries go out and no peer is asked twice.
void test_silent_crowd_bounded_per_window() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);
  for (uint8_t t = 1; t <= 6; ++t) g_nearby.push_back(makePeer(t, Color(), "crowd"));

  const uint32_t window = lamp::lioness::kColorQueryRetryMs;
  for (uint32_t t = 0; t <= 10 * window; t += 7) {
    set_mock_millis(t);
    ambient.control();
  }

  TEST_ASSERT_TRUE(g_queryCount > 3);   // the rotation does reach new peers
  for (size_t i = 0; i < g_queryCount; ++i) {
    int inWindow = 0;
    for (size_t j = i; j < g_queryCount && g_queries[j].ms - g_queries[i].ms < window; ++j) {
      ++inWindow;
      if (j > i) TEST_ASSERT_NOT_EQUAL(g_queries[i].tag, g_queries[j].tag);
    }
    TEST_ASSERT_TRUE(inWindow <= 3);
  }
}

// A crowd of answering peers: each lion switch asks at most once, so the
// query count tracks the director's switch rate rather than the frame rate.
void test_answering_crowd_queries_track_switches() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);
  for (uint8_t t = 1; t <= 6; ++t) g_nearby.push_back(makePeer(t, Color(), "crowd"));

  const uint32_t span = 20 * lamp::lioness::kStaggerPhaseMs;
  size_t answered = 0;
  for (uint32_t t = 0; t <= span; t += 16) {
    set_mock_millis(t);
    ambient.control();
    for (; answered < g_queryCount; ++answered) answer(ambient, g_queries[answered].tag);
  }

  TEST_ASSERT_TRUE(g_queryCount > 3);
  TEST_ASSERT_TRUE(g_queryCount <= span / lamp::lioness::kStaggerPhaseMs + 3);
}

// An answer frees its query slot at once: with three answered queries still
// inside their window, a lion moving onto a fourth peer asks immediately.
void test_answer_frees_query_slot() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);
  for (uint8_t t = 1; t <= 4; ++t) g_nearby.push_back(makePeer(t, Color(), "crowd"));

  ambient.control();   // seeds lions on peers 1-3, one query each
  TEST_ASSERT_EQUAL_size_t(3, g_queryCount);
  for (size_t i = 0; i < 3; ++i) answer(ambient, g_queries[i].tag);

  set_mock_millis(1);
  g_nearby.erase(g_nearby.begin());   // peer 1 leaves; its lion moves to peer 4
  ambient.control();
  TEST_ASSERT_EQUAL_size_t(4, g_queryCount);
  TEST_ASSERT_EQUAL_UINT8(4, g_queries[3].tag);
}

// Answers for peers no lion shows never evict a shown peer's stops.
void test_cache_keeps_shown_peers() {
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  LionsAmbientBehavior ambient(&fb);
  ambient.setBehaviorContext(&ctx);
  ambient.setMeshLink(&g_link);

  const uint8_t tags[3] = {0xA1, 0xB2, 0xC3};
  for (uint8_t t : tags) {
    g_nearby.push_back(makePeer(t, Color(), "peer"));
    answer(ambient, t);
  }
  ambient.control();
  answer(ambient, 0xD4);   // late answers from peers no lion shows
  answer(ambient, 0xE5);
  ambient.control();

  TEST_ASSERT_EQUAL_size_t(0, g_queryCount);
  for (uint32_t f = 0; f <= lamp::lioness::kCrossfadeFrames; ++f) ambient.draw();
  for (int z = 0; z < 3; ++z) TEST_ASSERT_EQUAL_UINT8(tags[z], fb.buffer[32 + z * 6].r);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ambient_draw_owns_only_lions_slice);
  RUN_TEST(test_ambient_composes_cached_peer_colour);
  RUN_TEST(test_ambient_delegates_greeting_to_greeting_object);
  RUN_TEST(test_greeting_latest_wins_restarts_pulse);
  RUN_TEST(test_greeting_draw_owns_only_lions_slice);
  RUN_TEST(test_ambient_draws_before_greeting_composites);
  RUN_TEST(test_ambient_steady_state_allocates_nothing);
  RUN_TEST(test_silent_peer_queried_once_per_window);
  RUN_TEST(test_silent_crowd_bounded_per_window);
  RUN_TEST(test_answering_crowd_queries_track_switches);
  RUN_TEST(test_answer_frees_query_slot);
  RUN_TEST(test_cache_keeps_shown_peers);
  return UNITY_END();
}
