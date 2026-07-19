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
    }
}

void AuroraPaletteClient::setActive(bool on) {
    if (on == (state_ != State::Idle)) return;
    if (!on) {
        ws_.close();
        group_ = GroupResolve{};
        state_ = State::Idle;
        Serial.println("[client] aurora client idle");
        return;
    }
    for (auto& z : zones_) z.resolvedId = String();
    // Back-dated so the first Discovering pass queries immediately.
    lastDiscoverMs_ = millis() - kDiscoverRetryMs;
    state_ = staticHost_ ? State::Connecting : State::Discovering;
    Serial.println("[client] aurora client armed");
}

void AuroraPaletteClient::loop() {
    switch (state_) {
        case State::Discovering:
            // Skip mDNS until STA is up. Without this gate the wisp issues
            // MDNS.queryService("aurora","tcp") even when ssid='';
            // each query blocks the WiFi task ~50-300ms and starves the
            // ESP-NOW send-callback path, causing bursty paint FAILs.
            if (!WiFi.isConnected()) break;
            if (millis() - lastDiscoverMs_ < kDiscoverRetryMs) break;
            lastDiscoverMs_ = millis();
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

void AuroraPaletteClient::serviceFetches() {
    if (millis() - lastFetchMs_ < kFetchRetryMs) return;
    if (group_.active) { serviceGroupResolve(); return; }
    // At most one blocking GET per pass (keeps ws_.poll() responsive).
    for (auto& z : zones_) {
        if (z.desiredId.length() == 0 || z.desiredId == z.resolvedId) continue;
        lastFetchMs_ = millis();
        Palette p;
        if (!fetcher_.fetchById(z.desiredId.c_str(), p)) return;
        if (p.isGroup && !p.childIds.empty()) {
            group_.active = true;
            group_.zone = z.zone;
            group_.desiredId = z.desiredId;
            group_.acc = Palette{};
            group_.acc.id = p.id;
            group_.acc.name = p.name;
            group_.acc.isGroup = true;
            group_.acc.childIds = p.childIds;
            group_.nextChild = 0;
        } else {
            z.resolvedId = z.desiredId;
            if (onPalette_) onPalette_(z.zone, p);
        }
        return;
    }
}

void AuroraPaletteClient::serviceGroupResolve() {
    ZoneState* z = nullptr;
    for (auto& s : zones_) {
        if (s.zone == group_.zone) { z = &s; break; }
    }
    // Abandon if the zone's desired palette moved on mid-resolve.
    if (!z || z->desiredId != group_.desiredId) {
        group_ = GroupResolve{};
        return;
    }
    lastFetchMs_ = millis();
    Palette child;
    // A failed child fetch is skipped, not retried.
    if (fetcher_.fetchById(group_.acc.childIds[group_.nextChild].c_str(),
                           child)) {
        group_.acc.hexColors.insert(group_.acc.hexColors.end(),
                                    child.hexColors.begin(),
                                    child.hexColors.end());
        group_.acc.colors.insert(group_.acc.colors.end(),
                                 child.colors.begin(), child.colors.end());
    }
    if (++group_.nextChild < group_.acc.childIds.size()) return;
    z->resolvedId = z->desiredId;
    if (onPalette_) onPalette_(z->zone, group_.acc);
    group_ = GroupResolve{};
}

void AuroraPaletteClient::handleFrame(const uint8_t* data, size_t len) {
    const DecodedNotification& d = NotificationCodec::decode(data, len);
    if (!d.ok) {
        Serial.printf("[client] ws frame decode failed (len=%u), dropped\n",
                      (unsigned)len);
        return;
    }

    if (d.type == aurora_NotificationType_CACHE_INVALIDATE) {
        // A palette's colors may have changed; force re-resolution of all
        // zones and drop any half-built group result.
        for (auto& z : zones_) z.resolvedId = String();
        group_ = GroupResolve{};
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
