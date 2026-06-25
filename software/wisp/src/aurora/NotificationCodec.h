#pragma once
#include <cstdint>
#include <cstddef>
#include "aurora_notifications.pb.h"

// Pure (host-portable). Decompresses (if needed) then decodes a NotificationEnvelope
// and, for known types, the inner payload. Dispatch is on the envelope `type` enum.
struct DecodedNotification {
    bool ok = false;
    aurora_NotificationType type = (aurora_NotificationType)0;
    char type_url[128] = {0};
    bool hasPalette = false;
    aurora_PaletteState palette = aurora_PaletteState_init_zero;
    bool hasPattern = false;
    aurora_PatternState pattern = aurora_PatternState_init_zero;
};

namespace NotificationCodec {
    // frame = raw bytes from the WebSocket (possibly compressed).
    DecodedNotification decode(const uint8_t* frame, size_t len);
}
