// Native-host tests for ExpressionManager unicast + crowd-exclude.
//
// Pins:
//   1. sendInvocationTo: calls sendCommand exactly once with the target mac.
//   2. broadcastInvocation(inv, &excluded): omits excluded mac, includes others.
//   3. broadcastInvocation(inv): no-exclude path; all nearby macs get a command.
//   4. sendInvocationTo with oversized payload is dropped (no sendCommand call).
//   5. sendInvocationTo no-ops when no MeshLink wired.
//
// Mirrors the logic (not the real ExpressionManager) to stay free of Arduino /
// FreeRTOS headers. Same approach as test_cascade_fanout.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>

// Satisfy COMMAND_MAX_PAYLOAD without the full protocol stack.
static constexpr size_t kCommandMaxPayload = 232;

// --- Minimal stubs ---

namespace lamp {

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
};

struct ExpressionInvocation {
  std::string type;
  std::vector<Color> colors;
  uint8_t target = 3;
  std::map<std::string, uint32_t> parameters;
  uint32_t delayMs = 0;
};

static void serializeInvocation(const ExpressionInvocation& inv, std::string& out) {
  // Minimal JSON serializer, enough to produce a non-empty, size-checkable string.
  out = "{\"type\":\"" + inv.type + "\"}";
}

struct RosterEntry {
  uint8_t mac[6] = {0};
  bool hasMac = false;
  int8_t lastRssi = -60;
};

// --- Fake MeshLink ---

struct SendRecord {
  uint8_t mac[6];
  std::string payload;
};

class FakeMeshLink {
 public:
  std::vector<SendRecord> sent;

  void clear() { sent.clear(); }

  bool sendCommand(const uint8_t targetMac[6], const uint8_t* json, size_t len) {
    SendRecord r;
    std::memcpy(r.mac, targetMac, 6);
    r.payload.assign(reinterpret_cast<const char*>(json), len);
    sent.push_back(r);
    return true;
  }

  bool isOtaInProgress() const { return false; }
};

// --- Logic mirrors ---

// Mirror of sendInvocationTo: serialize, size-guard, call sendCommand once.
static bool sendInvocationTo(FakeMeshLink* link, const uint8_t mac[6],
                             const ExpressionInvocation& inv) {
  if (!link) return false;
  std::string json;
  serializeInvocation(inv, json);
  if (json.size() > kCommandMaxPayload) return false;
  return link->sendCommand(mac, reinterpret_cast<const uint8_t*>(json.data()),
                           json.size());
}

// Mirror of broadcastInvocation fan-out with optional excludeMac. Mirrors
// maybeCascade's target-loop filter (skip self + excluded mac).
static void broadcastInvocationTo(FakeMeshLink* link,
                                  const ExpressionInvocation& inv,
                                  const std::vector<RosterEntry>& peers,
                                  const uint8_t* excludeMac = nullptr) {
  if (!link) return;
  if (link->isOtaInProgress()) return;

  std::string json;
  serializeInvocation(inv, json);
  if (json.empty() || json.size() > kCommandMaxPayload) return;

  for (const auto& p : peers) {
    if (!p.hasMac) continue;
    if (excludeMac && std::memcmp(p.mac, excludeMac, 6) == 0) continue;
    link->sendCommand(p.mac, reinterpret_cast<const uint8_t*>(json.data()),
                      json.size());
  }
}

}  // namespace lamp

// --- Helpers ---

static const uint8_t kMacA[6] = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05};
static const uint8_t kMacB[6] = {0xBB, 0x01, 0x02, 0x03, 0x04, 0x05};
static const uint8_t kMacC[6] = {0xCC, 0x01, 0x02, 0x03, 0x04, 0x05};

static lamp::RosterEntry makePeer(const uint8_t mac[6]) {
  lamp::RosterEntry p;
  std::memcpy(p.mac, mac, 6);
  p.hasMac = true;
  return p;
}

void setUp(void) {}
void tearDown(void) {}

// --- sendInvocationTo tests ---

void test_send_invocation_to_calls_sendcommand_once() {
  lamp::FakeMeshLink link;
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";

  lamp::sendInvocationTo(&link, kMacA, inv);

  TEST_ASSERT_EQUAL_UINT(1, link.sent.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, link.sent[0].mac, 6);
}

void test_send_invocation_to_no_op_without_meshlink() {
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";
  // Should not crash; returns false and calls nothing.
  const bool result = lamp::sendInvocationTo(nullptr, kMacA, inv);
  TEST_ASSERT_FALSE(result);
}

void test_send_invocation_to_drops_oversized_payload() {
  lamp::FakeMeshLink link;
  lamp::ExpressionInvocation inv;
  // Build a type string long enough to exceed kCommandMaxPayload when serialized.
  inv.type = std::string(kCommandMaxPayload + 10, 'x');

  lamp::sendInvocationTo(&link, kMacA, inv);

  TEST_ASSERT_EQUAL_UINT(0, link.sent.size());
}

// --- broadcastInvocation with excludeMac tests ---

void test_broadcast_excludes_specified_mac() {
  lamp::FakeMeshLink link;
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";

  std::vector<lamp::RosterEntry> peers = {
    makePeer(kMacA),  // excluded
    makePeer(kMacB),  // included
    makePeer(kMacC),  // included
  };

  lamp::broadcastInvocationTo(&link, inv, peers, kMacA);

  TEST_ASSERT_EQUAL_UINT(2, link.sent.size());
  // Neither sent mac should be kMacA.
  for (const auto& r : link.sent) {
    TEST_ASSERT_FALSE(std::memcmp(r.mac, kMacA, 6) == 0);
  }
  // kMacB and kMacC present.
  const bool hasB = std::any_of(link.sent.begin(), link.sent.end(),
    [](const lamp::SendRecord& r){ return std::memcmp(r.mac, kMacB, 6) == 0; });
  const bool hasC = std::any_of(link.sent.begin(), link.sent.end(),
    [](const lamp::SendRecord& r){ return std::memcmp(r.mac, kMacC, 6) == 0; });
  TEST_ASSERT_TRUE(hasB);
  TEST_ASSERT_TRUE(hasC);
}

void test_broadcast_no_exclude_sends_to_all() {
  lamp::FakeMeshLink link;
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";

  std::vector<lamp::RosterEntry> peers = {
    makePeer(kMacA),
    makePeer(kMacB),
  };

  lamp::broadcastInvocationTo(&link, inv, peers);  // no excludeMac

  TEST_ASSERT_EQUAL_UINT(2, link.sent.size());
}

void test_broadcast_skips_peers_without_mac() {
  lamp::FakeMeshLink link;
  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";

  lamp::RosterEntry noMac;
  noMac.hasMac = false;  // BLE-only peer with no HELLO yet

  std::vector<lamp::RosterEntry> peers = { noMac, makePeer(kMacB) };

  lamp::broadcastInvocationTo(&link, inv, peers);

  TEST_ASSERT_EQUAL_UINT(1, link.sent.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacB, link.sent[0].mac, 6);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_send_invocation_to_calls_sendcommand_once);
  RUN_TEST(test_send_invocation_to_no_op_without_meshlink);
  RUN_TEST(test_send_invocation_to_drops_oversized_payload);
  RUN_TEST(test_broadcast_excludes_specified_mac);
  RUN_TEST(test_broadcast_no_exclude_sends_to_all);
  RUN_TEST(test_broadcast_skips_peers_without_mac);

  return UNITY_END();
}
