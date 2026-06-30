#include "hello_emitter.hpp"
#include "ota_protocol.hpp"
#include "../../util/color.hpp"

#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include "catch_ota.hpp"
#include "espnow_link.hpp"
#include <esp_wifi.h>
#endif

namespace catch_ota {

namespace {
static uint32_t sLastHelloMs = 0;
static constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Identity advertised in the HELLO, set once at begin() so the lamp shows its
// real name + colors on the mesh while it waits to be upgraded.
static char    sName[HELLO_MAX_NAME + 1] = {0};
static uint8_t sBaseRGBW[4]  = {0, 0, 0, 0};
static uint8_t sShadeRGBW[4] = {0, 0, 0, 0};
}  // namespace

void helloSetIdentity(const char* name, const lamp::Color& base,
                      const lamp::Color& shade) {
    if (name) {
        std::strncpy(sName, name, HELLO_MAX_NAME);
        sName[HELLO_MAX_NAME] = '\0';
    }
    sBaseRGBW[0]  = base.r;  sBaseRGBW[1]  = base.g;
    sBaseRGBW[2]  = base.b;  sBaseRGBW[3]  = base.w;
    sShadeRGBW[0] = shade.r; sShadeRGBW[1] = shade.g;
    sShadeRGBW[2] = shade.b; sShadeRGBW[3] = shade.w;
}

size_t composeHello(uint8_t* buf, const uint8_t mac[6], const char* name) {
    const size_t nameLen = name ? std::strlen(name) : 0;
    return buildHello(buf, HELLO_MAX_SIZE, 0u, mac, sShadeRGBW, sBaseRGBW, 1u,
                      name, nameLen);
}

void helloTick(uint32_t nowMs) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (isInProgress()) return;
    if (nowMs - sLastHelloMs < kHelloIntervalMs) return;
    sLastHelloMs = nowMs;

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    uint8_t buf[HELLO_MAX_SIZE];
    const size_t len = composeHello(buf, mac, sName);
    if (len > 0) {
        espnowSend(kBroadcastMac, buf, len);
    }
#else
    (void)nowMs;
#endif
}

}  // namespace catch_ota
