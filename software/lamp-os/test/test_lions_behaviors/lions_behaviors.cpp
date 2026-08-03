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
//
// Adafruit_NeoPixel + esp_system are stubbed under test/native_stubs/.
// forEachNearby / forEachArrival and the two mesh/expression sinks the
// behaviors reference are stubbed here so control() runs without the full
// roster / mesh stack.

#include <unity.h>

#include <cstdint>
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
                                          const uint8_t*, bool) { return false; }
bool MeshLink::sendColorQuery(const uint8_t*) { return true; }
}  // namespace lamp

static Adafruit_NeoPixel neoMain;
static Adafruit_NeoPixel neoLions;

void setUp() { g_nearby.clear(); g_arrivals.clear(); }
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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ambient_draw_owns_only_lions_slice);
  RUN_TEST(test_ambient_composes_cached_peer_colour);
  RUN_TEST(test_ambient_delegates_greeting_to_greeting_object);
  RUN_TEST(test_greeting_latest_wins_restarts_pulse);
  RUN_TEST(test_greeting_draw_owns_only_lions_slice);
  RUN_TEST(test_ambient_draws_before_greeting_composites);
  return UNITY_END();
}
