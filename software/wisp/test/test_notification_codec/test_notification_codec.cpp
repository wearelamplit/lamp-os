#include <unity.h>
#include <vector>
#include <cstring>
#include "pb_encode.h"
#include "aurora_notifications.pb.h"
#include "NotificationCodec.h"
#include "Compression.h"
#include "miniz.h"

// Build a real PALETTE_STATE envelope (uncompressed) to feed the codec.
static std::vector<uint8_t> buildPaletteEnvelope() {
    aurora_PaletteState ps = aurora_PaletteState_init_zero;
    ps.states_count = 1;
    ps.states[0].has_zone = true; ps.states[0].zone = 2;
    ps.states[0].has_active_color_palette_id = true;
    std::strcpy(ps.states[0].active_color_palette_id, "palette-abc");
    uint8_t inner[256]; pb_ostream_t is = pb_ostream_from_buffer(inner, sizeof inner);
    TEST_ASSERT_TRUE(pb_encode(&is, aurora_PaletteState_fields, &ps));

    aurora_NotificationEnvelope env = aurora_NotificationEnvelope_init_zero;
    env.has_type = true; env.type = aurora_NotificationType_PALETTE_STATE;
    env.has_msg = true;
    env.msg.has_type_url = true;
    std::strcpy(env.msg.type_url, "type.googleapis.com/AURORA.Command.PaletteState");
    env.msg.has_value = true;
    env.msg.value.size = is.bytes_written;
    std::memcpy(env.msg.value.bytes, inner, is.bytes_written);

    uint8_t buf[512]; pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof buf);
    TEST_ASSERT_TRUE(pb_encode(&os, aurora_NotificationEnvelope_fields, &env));
    return std::vector<uint8_t>(buf, buf + os.bytes_written);
}

void test_decodes_palette_state(void) {
    auto frame = buildPaletteEnvelope();
    DecodedNotification d = NotificationCodec::decode(frame.data(), frame.size());
    TEST_ASSERT_TRUE(d.ok);
    TEST_ASSERT_EQUAL(aurora_NotificationType_PALETTE_STATE, d.type);
    TEST_ASSERT_TRUE(d.hasPalette);
    TEST_ASSERT_EQUAL(1, d.palette.states_count);
    TEST_ASSERT_EQUAL(2, d.palette.states[0].zone);
    TEST_ASSERT_EQUAL_STRING("palette-abc",
                             d.palette.states[0].active_color_palette_id);
}

void test_rejects_oversized_inflate(void) {
    std::vector<uint8_t> big(64 * 1024, 0);
    mz_ulong zlen = mz_compressBound(static_cast<mz_ulong>(big.size()));
    std::vector<uint8_t> z(zlen);
    TEST_ASSERT_EQUAL(MZ_OK, mz_compress(z.data(), &zlen, big.data(),
                                         static_cast<mz_ulong>(big.size())));
    std::vector<uint8_t> out;
    bool ok = Compression::maybeInflate(z.data(), static_cast<size_t>(zlen),
                                        aurora_NotificationEnvelope_size, out);
    TEST_ASSERT_FALSE(ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_decodes_palette_state);
    RUN_TEST(test_rejects_oversized_inflate);
    return UNITY_END();
}
