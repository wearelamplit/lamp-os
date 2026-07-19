#include "NotificationCodec.h"
#include "Compression.h"
#include "pb_decode.h"
#include <vector>
#include <cstring>

namespace NotificationCodec {

// ~4.3 KB of nanopb structs; statics keep them off the 8 KB loop stack.
// memset over {}-assignment: the assignment would materialize the same-size
// temporary on the stack.
static DecodedNotification s_out;
static aurora_NotificationEnvelope s_env;

const DecodedNotification& decode(const uint8_t* frame, size_t len) {
    std::memset(&s_out, 0, sizeof s_out);
    std::memset(&s_env, 0, sizeof s_env);
    DecodedNotification& out = s_out;
    aurora_NotificationEnvelope& env = s_env;

    std::vector<uint8_t> raw;
    if (!Compression::maybeInflate(frame, len, aurora_NotificationEnvelope_size, raw)) return out;

    pb_istream_t es = pb_istream_from_buffer(raw.data(), raw.size());
    if (!pb_decode(&es, aurora_NotificationEnvelope_fields, &env)) return out;

    out.type = env.has_type ? env.type : (aurora_NotificationType)0;
    if (env.has_msg && env.msg.has_type_url) {
        std::strncpy(out.type_url, env.msg.type_url, sizeof(out.type_url) - 1);
    }

    if (!env.has_msg || !env.msg.has_value) { out.ok = true; return out; }

    pb_istream_t vs = pb_istream_from_buffer(env.msg.value.bytes,
                                             env.msg.value.size);
    switch (out.type) {
        case aurora_NotificationType_PALETTE_STATE:
            if (pb_decode(&vs, aurora_PaletteState_fields, &out.palette))
                out.hasPalette = true;
            break;
        case aurora_NotificationType_PATTERN_STATE:
            if (pb_decode(&vs, aurora_PatternState_fields, &out.pattern))
                out.hasPattern = true;
            break;
        default:
            break;  // CACHE_INVALIDATE etc. Type is enough for callers.
    }
    out.ok = true;
    return out;
}

}  // namespace NotificationCodec
