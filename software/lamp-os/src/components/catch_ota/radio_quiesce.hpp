#pragma once

namespace catch_ota {

// Boot: keep modem sleep off so ESP-NOW RX stays live while idle. The softAP is
// up on LAMP_ESPNOW_CHANNEL (toApMode under CATCH_OTA), so web config and mesh
// RX coexist. Called once from catch_ota::begin().
void radioBeginDiscovery();

// On the first committed OFFER: tear down softAP + BLE → STA-only on
// LAMP_ESPNOW_CHANNEL, giving ESP-NOW the single radio for the transfer. The
// mode switch clears the ESP-NOW peer table, so the caller must re-register the
// sender peer afterward.
void radioEnterOtaMode();

}  // namespace catch_ota
