#include "AuroraPaletteClient.h"
#include <WiFi.h>
#include "NotificationCodec.h"
#include "SubscriptionEncoder.h"

void AuroraPaletteClient::begin() {
    ws_.onBinary([this](const uint8_t* d, size_t n){ handleFrame(d, n); });
    ws_.onOpen([this](){ sendSubscriptions(); });
    if (staticHost_) {
        ws_.setTarget(staticIp_, staticPort_);
        fetcher_.setTarget(staticIp_, staticPort_);
        Serial.printf("[client] static host %s:%u (skipping mDNS)\n",
                      staticIp_.toString().c_str(), staticPort_);
        state_ = State::Connecting;
    } else {
        state_ = State::Discovering;
    }
}

void AuroraPaletteClient::loop() {
    switch (state_) {
        case State::Discovering:
            // Skip mDNS until STA is up. Without this gate the wisp issues
            // MDNS.queryService("aurora","tcp") every ~3s even when ssid='';
            // each query blocks the WiFi task ~50-300ms and starves the
            // ESP-NOW send-callback path, causing bursty paint FAILs.
            if (!WiFi.isConnected()) break;
            if (discovery_.discover()) {
                ws_.setTarget(discovery_.ip(), discovery_.port());
                fetcher_.setTarget(discovery_.ip(), discovery_.port());
                state_ = State::Connecting;
            }
            break;
        case State::Connecting:
        case State::Streaming:
            ws_.loop();
            if (ws_.isConnected()) state_ = State::Streaming;
            // Repeated WS failures likely mean the device moved (DHCP) or went
            // away; fall back to mDNS (nothing to re-discover with a static host).
            if (!staticHost_ && ws_.consecutiveFailures() >= kRediscoverFails) {
                Serial.println("[client] too many WS failures; re-discovering");
                state_ = State::Discovering;
                break;
            }
            serviceFetches();
            break;
        default:
            break;
    }
}

void AuroraPaletteClient::sendSubscriptions() {
    // INSTANCE_INFO first, then the full replace_all subscription set.
    auto info = SubscriptionEncoder::instanceInfo(instanceId_.c_str());
    ws_.send(info.data(), info.size());

    const aurora_NotificationType types[] = {
        aurora_NotificationType_CACHE_INVALIDATE,
        aurora_NotificationType_PALETTE_STATE,
        aurora_NotificationType_PATTERN_STATE,
    };
    auto sub = SubscriptionEncoder::subscriptionRequest(types, 3);
    ws_.send(sub.data(), sub.size());
    Serial.println("[client] sent INSTANCE_INFO + subscriptions");
}

void AuroraPaletteClient::setDesired(int zone, const char* paletteId) {
    if (!paletteId || !*paletteId) return;
    for (auto& z : zones_) {
        if (z.zone == zone) { z.desiredId = paletteId; return; }
    }
    zones_.push_back(ZoneState{zone, String(paletteId), String()});
}

// Resolve a palette id to concrete colors. GROUP palettes have no colors of
// their own, so fetch each child and aggregate their colors.
bool AuroraPaletteClient::resolvePalette(const char* id, Palette& out) {
    Palette base;
    if (!fetcher_.fetchById(id, base)) return false;
    if (!base.isGroup) { out = base; return true; }

    out = Palette{};
    out.id = base.id;
    out.name = base.name;
    out.isGroup = true;
    out.childIds = base.childIds;
    for (const auto& cid : base.childIds) {
        Palette child;
        if (fetcher_.fetchById(cid.c_str(), child)) {
            out.hexColors.insert(out.hexColors.end(),
                                 child.hexColors.begin(), child.hexColors.end());
            out.colors.insert(out.colors.end(),
                              child.colors.begin(), child.colors.end());
        }
    }
    return true;
}

void AuroraPaletteClient::serviceFetches() {
    if (millis() - lastFetchMs_ < kFetchRetryMs) return;
    // Resolve at most one stale zone per pass (keeps ws_.poll() responsive).
    for (auto& z : zones_) {
        if (z.desiredId.length() == 0 || z.desiredId == z.resolvedId) continue;
        lastFetchMs_ = millis();
        Palette p;
        if (resolvePalette(z.desiredId.c_str(), p)) {
            z.resolvedId = z.desiredId;
            if (onPalette_) onPalette_(z.zone, p);
        }
        return;  // one fetch per pass
    }
}

void AuroraPaletteClient::handleFrame(const uint8_t* data, size_t len) {
    DecodedNotification d = NotificationCodec::decode(data, len);
    if (!d.ok) {
        Serial.printf("[client] ws frame decode failed (len=%u), dropped\n",
                      (unsigned)len);
        return;
    }

    if (d.type == aurora_NotificationType_CACHE_INVALIDATE) {
        // A palette's colors may have changed; force re-resolution of all zones.
        for (auto& z : zones_) z.resolvedId = String();
        return;
    }
    if (d.hasPalette) {
        for (pb_size_t i = 0; i < d.palette.states_count; ++i) {
            const auto& z = d.palette.states[i];
            if (z.has_active_color_palette_id) {
                const int zoneNum = z.has_zone ? z.zone : 0;
                // Notify observed-zones tracking BEFORE setDesired so the
                // ZoneSelector sees every announced zone, not just resolved ones.
                if (onZoneObserved_) onZoneObserved_(zoneNum);
                setDesired(zoneNum, z.active_color_palette_id);
            }
        }
    }
}
