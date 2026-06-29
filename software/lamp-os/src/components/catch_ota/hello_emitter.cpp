#include "hello_emitter.hpp"
#include "ota_protocol.hpp"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include "catch_ota.hpp"
#include "espnow_link.hpp"
#include <esp_wifi.h>
#endif

namespace catch_ota {

namespace {
static uint32_t       sLastHelloMs  = 0;
static constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}  // namespace

size_t composeHello(uint8_t* buf, const uint8_t mac[6], const char* name) {
    static const uint8_t kZero[4] = {0, 0, 0, 0};
    const size_t nameLen = name ? strlen(name) : 0;
    return buildHello(buf, HELLO_MAX_SIZE, 0u, mac, kZero, kZero, 1u, name, nameLen);
}

void helloTick(uint32_t nowMs) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (isInProgress()) return;
    if (nowMs - sLastHelloMs < kHelloIntervalMs) return;
    sLastHelloMs = nowMs;

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    char name[10];
    snprintf(name, sizeof(name), "m-%02x%02x%02x", mac[3], mac[4], mac[5]);

    uint8_t buf[HELLO_MAX_SIZE];
    const size_t len = composeHello(buf, mac, name);
    if (len > 0) {
        espnowSend(kBroadcastMac, buf, len);
    }
#else
    (void)nowMs;
#endif
}

}  // namespace catch_ota
