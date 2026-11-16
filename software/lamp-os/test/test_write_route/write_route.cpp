// WriteRoute, the transport-free half of a routed BLE write: each refusal is
// named and posts nothing; an accepted write posts its bytes.

#include <unity.h>

#include <cstring>
#include <string>

#include "components/network/ble/write_route.cpp"

using ble_control::WriteRoute;

static std::string g_posted;
static int g_posts = 0;
static bool g_authed = true;
static bool g_decodeOk = true;
static bool g_decodeAuthed = true;
static std::string g_decoded;

static void post(const char* data, size_t len) {
  g_posted.assign(data, len);
  ++g_posts;
}
static bool auth(uint16_t) { return g_authed; }
static bool decode(const std::string&, uint16_t, const uint8_t*, const char*,
                   std::string& out, bool& authed) {
  if (!g_decodeOk) return false;
  out = g_decoded;
  authed = g_decodeAuthed;
  return true;
}

static const uint8_t kSalt[16] = {};

void setUp(void) {
  g_posted.clear();
  g_posts = 0;
  g_authed = true;
  g_decodeOk = true;
  g_decodeAuthed = true;
  g_decoded = "{\"a\":1}";
}
void tearDown(void) {}

static void assertRefused(const char* reason, const char* got) {
  TEST_ASSERT_NOT_NULL_MESSAGE(got, reason);
  TEST_ASSERT_EQUAL_STRING(reason, got);
  TEST_ASSERT_EQUAL_INT(0, g_posts);
}

void test_plaintext_write_posts_its_bytes() {
  const WriteRoute route(8, post, auth);
  TEST_ASSERT_NULL(route.deliver("abcdefgh", 1));
  TEST_ASSERT_EQUAL_INT(1, g_posts);
  TEST_ASSERT_EQUAL_STRING("abcdefgh", g_posted.c_str());
}

void test_plaintext_refusals_are_named() {
  const WriteRoute route(8, post, auth);
  g_authed = false;
  assertRefused("unauthenticated", route.deliver("abc", 1));
  g_authed = true;
  assertRefused("oversize", route.deliver("abcdefghi", 1));
  assertRefused("empty", route.deliver("", 1));
}

void test_plaintext_empty_passes_when_allowed() {
  WriteRoute route(4, post, auth);
  route.setAllowEmpty(true);
  TEST_ASSERT_NULL(route.deliver("", 1));
  TEST_ASSERT_EQUAL_INT(1, g_posts);
  TEST_ASSERT_TRUE(g_posted.empty());
}

void test_decoded_write_posts_the_decoded_bytes() {
  const WriteRoute route(16, post, auth, decode, kSalt, "op");
  TEST_ASSERT_NULL(route.deliver(std::string(40, 'x'), 1));
  TEST_ASSERT_EQUAL_INT(1, g_posts);
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", g_posted.c_str());
}

// The wire bound is the decoded bound plus 64 bytes of GCM overhead.
void test_decoded_refusals_are_named() {
  const WriteRoute route(16, post, auth, decode, kSalt, "op");
  assertRefused("oversize", route.deliver(std::string(16 + 65, 'x'), 1));
  g_decodeOk = false;
  assertRefused("decode failed", route.deliver("x", 1));
  g_decodeOk = true;
  g_decodeAuthed = false;
  assertRefused("unauthenticated", route.deliver("x", 1));
  g_decodeAuthed = true;
  g_decoded = std::string(17, 'y');
  assertRefused("oversize", route.deliver("x", 1));
  g_decoded.clear();
  assertRefused("empty", route.deliver("x", 1));
}

void test_decoded_wire_bound_admits_its_limit() {
  const WriteRoute route(16, post, auth, decode, kSalt, "op");
  TEST_ASSERT_NULL(route.deliver(std::string(16 + 64, 'x'), 1));
  TEST_ASSERT_EQUAL_INT(1, g_posts);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_plaintext_write_posts_its_bytes);
  RUN_TEST(test_plaintext_refusals_are_named);
  RUN_TEST(test_plaintext_empty_passes_when_allowed);
  RUN_TEST(test_decoded_write_posts_the_decoded_bytes);
  RUN_TEST(test_decoded_refusals_are_named);
  RUN_TEST(test_decoded_wire_bound_admits_its_limit);
  return UNITY_END();
}
