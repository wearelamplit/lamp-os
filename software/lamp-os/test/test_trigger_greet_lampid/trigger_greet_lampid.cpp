#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "util/bd_addr.hpp"

namespace {

struct RosterEntry {
  std::string name;
  uint8_t mac[6] = {0};
  bool hasMac = false;
};

bool findByMac(const std::vector<RosterEntry>& store, const uint8_t mac[6],
               RosterEntry& out) {
  for (const auto& e : store) {
    if (e.hasMac && std::memcmp(e.mac, mac, 6) == 0) {
      out = e;
      return true;
    }
  }
  return false;
}

struct FakeGreetable {
  bool triggered = false;
  bool active = false;
  RosterEntry lastPeer;
  void triggerGreeting(const RosterEntry& peer) {
    triggered = true;
    lastPeer = peer;
  }
};

// Mirrors the "triggerGreet" dispatch in lamp_test_action.cpp: keep the
// lampId-string → mac-bytes → findByMac → active-guard chain in sync.
bool dispatchTriggerGreet(const char* lampId,
                          const std::vector<RosterEntry>& roster,
                          FakeGreetable& greetable) {
  uint8_t mac[6];
  RosterEntry peer;
  if (!lamp::parseBdAddr(lampId, mac) || !findByMac(roster, mac, peer)) {
    return false;
  }
  if (greetable.active) return false;
  greetable.triggerGreeting(peer);
  return true;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_trigger_greet_resolves_by_mac() {
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  RosterEntry e;
  e.name = "flora";
  std::memcpy(e.mac, mac, 6);
  e.hasMac = true;
  std::vector<RosterEntry> roster = {e};

  FakeGreetable greetable;
  TEST_ASSERT_TRUE(
      dispatchTriggerGreet("AA:BB:CC:DD:EE:FF", roster, greetable));
  TEST_ASSERT_TRUE(greetable.triggered);
  TEST_ASSERT_EQUAL_STRING("flora", greetable.lastPeer.name.c_str());
}

void test_trigger_greet_unknown_lampid_no_op() {
  std::vector<RosterEntry> roster;
  FakeGreetable greetable;
  TEST_ASSERT_FALSE(
      dispatchTriggerGreet("11:22:33:44:55:66", roster, greetable));
  TEST_ASSERT_FALSE(greetable.triggered);
}

void test_trigger_greet_malformed_lampid_no_op() {
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  RosterEntry e;
  e.name = "flora";
  std::memcpy(e.mac, mac, 6);
  e.hasMac = true;
  std::vector<RosterEntry> roster = {e};

  FakeGreetable greetable;
  TEST_ASSERT_FALSE(dispatchTriggerGreet("not-a-mac", roster, greetable));
  TEST_ASSERT_FALSE(greetable.triggered);
}

void test_trigger_greet_ignored_while_active() {
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  RosterEntry e;
  e.name = "flora";
  std::memcpy(e.mac, mac, 6);
  e.hasMac = true;
  std::vector<RosterEntry> roster = {e};

  FakeGreetable greetable;
  greetable.active = true;
  TEST_ASSERT_FALSE(
      dispatchTriggerGreet("AA:BB:CC:DD:EE:FF", roster, greetable));
  TEST_ASSERT_FALSE(greetable.triggered);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_trigger_greet_resolves_by_mac);
  RUN_TEST(test_trigger_greet_unknown_lampid_no_op);
  RUN_TEST(test_trigger_greet_malformed_lampid_no_op);
  RUN_TEST(test_trigger_greet_ignored_while_active);
  return UNITY_END();
}
