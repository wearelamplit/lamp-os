#pragma once

namespace catch_ota {

// Boot/discovery: keep the lamp a normal main lamp — softAP stays up on
// LAMP_ESPNOW_CHANNEL (web config, the only control path on main) — and just
// freeze stage-mode / network-scan so the radio stays on channel for ESP-NOW
// discovery. Called once from catch_ota::begin().
void radioBeginDiscovery();

// On the first committed OFFER: tear down softAP + BLE → STA-only on
// LAMP_ESPNOW_CHANNEL, giving ESP-NOW the single radio for the transfer. The
// mode switch clears the ESP-NOW peer table, so the caller must re-register the
// sender peer afterward.
void radioEnterOtaMode();

}  // namespace catch_ota
