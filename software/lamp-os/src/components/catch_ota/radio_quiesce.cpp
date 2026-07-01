#include "radio_quiesce.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <WiFi.h>
#include <esp_wifi.h>

#include "../network/bluetooth.hpp"
#endif

namespace catch_ota {

void radioBeginDiscovery() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // Called when ESP-NOW listening comes up (a mesh sender is nearby). Keep
    // modem sleep off so OFFER/CHUNK RX isn't missed, and pin the ESP-NOW channel
    // in case we woke while stage mode had moved the radio. The radio is fully
    // seized (radioEnterOtaMode) only when a transfer commits.
    WiFi.setSleep(false);
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
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
