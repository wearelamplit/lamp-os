#include "catch_ota.hpp"

#include "espnow_link.hpp"
#include "hello_emitter.hpp"
#include "ota_protocol.hpp"
#include "ota_receiver.hpp"
#include "radio_quiesce.hpp"
#include "rollback_breaker.hpp"

#include "../network/bluetooth.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>  // millis()
#endif

namespace catch_ota {

// main carries no real firmware version. Dev offers must exceed this sentinel
// so the lamp cannot accept a same-or-older build from a dev pusher.
static constexpr uint32_t kOurVersion = 1;

// ESP-NOW OTA listening is dormant until a mesh-capable sender is seen over BLE:
// running it always camps the radio and starves the BLE scan (breaks stage/home).
// When a sender appears, listen for one window; if no OFFER lands, go dormant and
// let BLE recover before looking again. Tunable; less listening is fine since a
// lamp near a sender stays put and OFFERs retransmit.
static constexpr uint32_t kListenWindowMs = 60000;
static constexpr uint32_t kCooldownMs     = 60000;

static bool     s_listening      = false;
static uint32_t s_listenStartMs  = 0;
static uint32_t s_dormantUntilMs = 0;

static void onEspnowRecv(const uint8_t mac[6], const uint8_t* data, size_t len) {
    const uint8_t msgType = inspect(data, len);

    if (msgType == MSG_FW_OFFER) {
        ParsedFwOffer offer{};
        if (!parseFwOffer(data, len, offer)) return;
        if (offer.version <= kOurVersion) return;                         // version gate
        if (!shouldAttempt(offer.sha256Prefix)) return;                   // circuit breaker
        ota_receiver::onOffer(offer, mac);  // records the breaker attempt at commit
    } else if (msgType == MSG_FW_CHUNK) {
        ParsedFwChunk chunk{};
        if (!parseFwChunk(data, len, chunk)) return;
        ota_receiver::onChunkOnLoop(chunk);
    } else if (msgType == MSG_FW_DONE) {
        ParsedFwDone done{};
        if (!parseFwDone(data, len, done)) return;
        ota_receiver::onDone(done);
    }
}

static void startListening(uint32_t nowMs) {
    radioBeginDiscovery();  // modem sleep off for the listen window
    espnowBegin(onEspnowRecv);
    s_listening = true;
    s_listenStartMs = nowMs;
}

static void stopListening(uint32_t nowMs) {
    espnowStop();
    s_listening = false;
    s_dormantUntilMs = nowMs + kCooldownMs;
}

void begin(const char* name, const lamp::Color& base, const lamp::Color& shade) {
    helloSetIdentity(name, base, shade);
    // ESP-NOW stays down. startListening() brings it up only once a mesh sender is
    // seen over BLE, so the lamp is a normal BLE/stage lamp until then.
}

void tick(uint32_t nowMs) {
    if (!s_listening) {
        if ((int32_t)(nowMs - s_dormantUntilMs) >= 0 && lamp::meshLampPresent()) {
            startListening(nowMs);
        }
        return;
    }

    espnowPoll();
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // espnowPoll() may run onOffer's multi-second upfront erase, which advances
    // the clock. Re-read so the timeouts in ota_receiver::tick compare against
    // the clock onOffer stamped its timestamps on, not a stale pre-erase nowMs.
    nowMs = millis();
#endif
    helloTick(nowMs);
    ota_receiver::tick(nowMs);

    // Release the radio after the listen window if nothing committed, so BLE
    // recovers; a live transfer reboots on its own, so never tear it down then.
    if (!ota_receiver::isInProgress() && nowMs - s_listenStartMs >= kListenWindowMs) {
        stopListening(nowMs);
    }
}

bool isInProgress() {
    return ota_receiver::isInProgress();
}

}  // namespace catch_ota
