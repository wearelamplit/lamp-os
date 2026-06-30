#include "catch_ota.hpp"

#include "espnow_link.hpp"
#include "hello_emitter.hpp"
#include "ota_protocol.hpp"
#include "ota_receiver.hpp"
#include "radio_quiesce.hpp"
#include "rollback_breaker.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>  // millis()
#endif

namespace catch_ota {

// main carries no real firmware version. Dev offers must exceed this sentinel
// so the lamp cannot accept a same-or-older build from a dev pusher.
static constexpr uint32_t kOurVersion = 1;

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

void begin(const char* name, const lamp::Color& base, const lamp::Color& shade) {
    helloSetIdentity(name, base, shade);
    radioBeginDiscovery();
    espnowBegin(onEspnowRecv);
}

void tick(uint32_t nowMs) {
    espnowPoll();
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // espnowPoll() may run onOffer's multi-second upfront erase, which advances
    // the clock. Re-read so the timeouts in ota_receiver::tick compare against
    // the clock onOffer stamped its timestamps on, not a stale pre-erase nowMs.
    nowMs = millis();
#endif
    helloTick(nowMs);
    ota_receiver::tick(nowMs);
}

bool isInProgress() {
    return ota_receiver::isInProgress();
}

}  // namespace catch_ota
