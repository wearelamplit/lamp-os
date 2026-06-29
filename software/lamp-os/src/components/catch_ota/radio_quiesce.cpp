#include "radio_quiesce.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <WiFi.h>
#include <esp_wifi.h>

#include "../network/bluetooth.hpp"
#include "../network/wifi.hpp"
#endif

namespace catch_ota {

void radioBeginDiscovery() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // Stay a normal main lamp during discovery: the softAP is up on the ESP-NOW
    // channel (toApMode brings it up there for CATCH_OTA) so the web config page
    // keeps working — that is the only control path on main. Just freeze
    // stage-mode / network-scan so nothing moves the radio off the channel, and
    // keep modem sleep off so ESP-NOW RX stays live. The softAP + BLE come down
    // only when a transfer commits (radioEnterOtaMode); a reboot restores them.
    lamp::otaInProgress = true;
    WiFi.setSleep(false);
#endif
}

void radioEnterOtaMode() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // A transfer committed — drop the softAP + BLE so ESP-NOW owns the single
    // radio for the streaming window (an active softAP makes directed chunk RX
    // unreliable). The mode switch clears the ESP-NOW peer table, so the caller
    // re-registers the sender as a unicast peer before the ACCEPT goes out.
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_STA);
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    WiFi.setSleep(false);
    lamp::bleStopScanNoRestart();
    lamp::bleStopAdvertising();
#endif
}

}  // namespace catch_ota
