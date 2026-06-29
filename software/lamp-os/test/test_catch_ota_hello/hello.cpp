// Round-trip test for the HELLO emitter's pure compose function.
// Includes the source directly — no test_build_src, -I src is set.

#include "../../src/components/catch_ota/hello_emitter.cpp"

#include <unity.h>

#include <cstdint>
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

namespace co = catch_ota;

// --------------------------------------------------------------------------
// test_emitted_hello_accepted_by_parser
// composeHello(buf, mac, "m-abc") must produce a frame that parseHello
// accepts. Asserts: buf[2]==0x05, tlv_count==0, name/MAC/firmwareVersion match.
// --------------------------------------------------------------------------
void test_emitted_hello_accepted_by_parser() {
    const uint8_t mac[6]  = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    const char*   name    = "m-abc";
    const size_t  nameLen = strlen(name);

    uint8_t buf[co::HELLO_MAX_SIZE];
    size_t len = co::composeHello(buf, mac, name);
    TEST_ASSERT_TRUE(len > 0);

    // Protocol version byte at [2] must be 0x05
    TEST_ASSERT_EQUAL_UINT8(0x05, buf[2]);

    // tlv_count byte immediately after the name field must be 0
    const size_t tlvCountOff = co::HELLO_FIXED_SIZE + 1 + nameLen;
    TEST_ASSERT_EQUAL_UINT8(0, buf[tlvCountOff]);

    // Parse round-trip
    co::ParsedHello ph{};
    bool ok = co::parseHello(buf, len, ph);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_MEMORY(mac, ph.sourceMac, 6);
    TEST_ASSERT_EQUAL_STRING(name, ph.name);
    TEST_ASSERT_EQUAL_UINT32(1u, ph.firmwareVersion);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_emitted_hello_accepted_by_parser);
    return UNITY_END();
}
