#include "NotificationCodec.h"
#include "Compression.h"
#include "pb_decode.h"
#include <vector>
#include <cstring>

namespace NotificationCodec {

DecodedNotification decode(const uint8_t* frame, size_t len) {
    DecodedNotification out;

    std::vector<uint8_t> raw;
    if (!Compression::maybeInflate(frame, len, raw)) return out;

    aurora_NotificationEnvelope env = aurora_NotificationEnvelope_init_zero;
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
            break;  // CACHE_INVALIDATE etc. — type is enough for callers.
    }
    out.ok = true;
    return out;
}

}  // namespace NotificationCodec
