#include "serial_console.hpp"

#include <WiFi.h>
#include <esp_wifi.h>

#include "config/wisp_config.hpp"
#include "config/zone_selector.hpp"
#include "fleet/lamp_inventory.hpp"
#include "net/mesh_link.hpp"
#include "net/stage_beacon.hpp"
#include "net/wifi_link.hpp"
#include "artnet/artnet_emitter.hpp"
#include "paint/paint_distributor.hpp"
#include "status/status_emitter.hpp"

namespace wisp {

SerialConsole::SerialConsole(PaintDistributor& paint, WispConfig& config,
                             ArtnetEmitter& artnet, StageBeacon& stage,
                             WifiLink& wifi, StatusEmitter& status,
                             LampInventory& inventory, ZoneSelector& zones,
                             SourceTransitionFn onSourceTransition)
    : paint_(paint),
      config_(config),
      artnet_(artnet),
      stage_(stage),
      wifi_(wifi),
      status_(status),
      inventory_(inventory),
      zones_(zones),
      onSourceTransition_(std::move(onSourceTransition)) {}

void SerialConsole::pump() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\r') continue;  // strip CR; macOS / Linux send LF only
    if (ch == '\n') {
      String cmd = buf_;
      cmd.trim();
      buf_ = String();
      handleCommand(cmd);
      continue;
    }
    if (buf_.length() < 128) buf_ += static_cast<char>(ch);
  }
}

String SerialConsole::formatVersion(uint32_t v) {
  uint8_t major = (v >> 16) & 0xFF;
  uint8_t minor = (v >> 8) & 0xFF;
  uint8_t patch = v & 0xFF;
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
  return String(buf);
}

// Re-sample millis() to avoid unsigned-subtraction wraparound if HELLOs
// arrived during auroraClient.loop() between the caller's snapshot and here.
void SerialConsole::dumpInventory() {
  const uint32_t nowMs = millis();
  auto roster = inventory_.snapshot();
  Serial.printf("[wisp] roster (%u lamp%s):\n",
                (unsigned)roster.size(), roster.size() == 1 ? "" : "s");
  for (const auto& e : roster) {
    const uint32_t ageMs = (nowMs >= e.lastSeenMs) ? nowMs - e.lastSeenMs : 0;
    Serial.printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-12s  fw=%s  age=%lums\n",
                  e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
                  e.name.c_str(), formatVersion(e.firmwareVersion).c_str(),
                  (unsigned long)ageMs);
  }
  Serial.printf("[wisp] zone=%d source=%s observed=%u\n",
                zones_.currentZone(),
                zoneSourceName(zones_.source()),
                (unsigned)zones_.observedCount());
}

void SerialConsole::handleCommand(const String& cmd) {
  if (cmd.length() == 0) return;
  if (cmd == "paint:on") {
    paint_.setPaintMode(true);
    Serial.println("[wisp.cmd] paint mode ON");
  } else if (cmd == "paint:off") {
    paint_.setPaintMode(false);
    Serial.println("[wisp.cmd] paint mode OFF");
  } else if (cmd == "src:off" || cmd == "src:manual" || cmd == "src:aurora") {
    const WispSourceMode m =
        cmd.endsWith("off")    ? WispSourceMode::Off
      : cmd.endsWith("manual") ? WispSourceMode::Manual
                               : WispSourceMode::Aurora;
    config_.setSourceMode(m);
    onSourceTransition_(m);
    status_.triggerOnChange();
    Serial.printf("[wisp.cmd] source mode = %s\n", cmd.c_str() + 4);
  } else if (cmd == "artnet:on") {
    artnet_.setEnabled(true);
  } else if (cmd == "artnet:off") {
    artnet_.setEnabled(false);
  } else if (cmd == "stage:on") {
    stage_.refreshAdvert();
  } else if (cmd == "stage:off") {
    stage_.stop();
  } else if (cmd == "wifi:show") {
    Serial.printf("[wifi] ssid='%s' connected=%d ip=%s\n",
                  wifi_.ssid().c_str(), wifi_.isConnected() ? 1 : 0,
                  WiFi.localIP().toString().c_str());
  } else if (cmd == "wifi:clear") {
    // WiFi.disconnect alone doesn't reset the radio channel; need
    // esp_wifi_set_channel to snap back to LAMP_ESPNOW_CHANNEL.
    config_.setWifi("", "");
    wifi_.reconnect();
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (stage_.isAdvertising()) {
      stage_.refreshAdvert();
    }
    Serial.printf("[wisp.cmd] wifi creds cleared; radio pinned to channel %d\n",
                  LAMP_ESPNOW_CHANNEL);
  } else if (cmd.startsWith("wifi:set ")) {
    // Format: "wifi:set <ssid> <pass>" — split on first space after prefix.
    int sp = cmd.indexOf(' ', 9);
    if (sp < 0 || sp == 9 || sp == (int)cmd.length() - 1) {
      Serial.println("[wisp.cmd] usage: wifi:set <ssid> <pass>");
      return;
    }
    String ssid = cmd.substring(9, sp);
    String pass = cmd.substring(sp + 1);
    config_.setWifi(ssid, pass);
    wifi_.reconnect();
    if (stage_.isAdvertising()) {
      stage_.refreshAdvert();
    }
    Serial.println("[wisp.cmd] wifi creds saved");
  } else {
    Serial.printf("[wisp.cmd] unknown command: %s\n", cmd.c_str());
  }
}

}  // namespace wisp
