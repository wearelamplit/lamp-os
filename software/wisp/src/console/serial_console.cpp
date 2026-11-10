#include "serial_console.hpp"

#include <WiFi.h>
#include <esp_wifi.h>

#include "config/wisp_config.hpp"
#include "fleet/lamp_inventory.hpp"
#include "net/mesh_link.hpp"
#include "net/stage_beacon.hpp"
#include "net/eth_link.hpp"
#include "net/wifi_link.hpp"
#include "osc/osc_listener.hpp"
#include "paint/paint_distributor.hpp"
#include "status/status_emitter.hpp"

namespace wisp {

SerialConsole::SerialConsole(PaintDistributor& paint, WispConfig& config,
                             StageBeacon& stage, WifiLink& wifi,
                             StatusEmitter& status, LampInventory& inventory,
                             SourceTransitionFn onSourceTransition)
    : paint_(paint),
      config_(config),
      stage_(stage),
      wifi_(wifi),
      status_(status),
      inventory_(inventory),
      onSourceTransition_(std::move(onSourceTransition)) {}

void SerialConsole::pump() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    const SlipDecoder::Result r =
        slip_.feed(static_cast<uint8_t>(ch), millis());
    if (r != SlipDecoder::Result::Text) {
      // A frame byte ends any partial text line, so the tail of a frame cut
      // off by the idle reset cannot join a command at the next frame.
      buf_ = String();
    }
    if (r == SlipDecoder::Result::Frame) {
      if (osc_) osc_->handlePacket(slip_.frame(), slip_.frameLen(), "serial");
      continue;
    }
    if (r == SlipDecoder::Result::Consumed) continue;
    if (ch == '\r') continue;  // strip CR from CRLF line endings
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

void SerialConsole::dumpInventory() {
#ifdef LAMP_DEBUG
  const uint32_t nowMs = millis();
  auto roster = inventory_.snapshot();
  Serial.printf("[wisp] roster (%u lamp%s):\n",
                (unsigned)roster.size(), roster.size() == 1 ? "" : "s");
  for (const auto& e : roster) {
    const int32_t sinceMs = static_cast<int32_t>(nowMs - e.lastSeenMs);
    const uint32_t ageMs = sinceMs > 0 ? static_cast<uint32_t>(sinceMs) : 0;
    Serial.printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-12s  fw=%s  age=%lums  rssi=%d\n",
                  e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
                  e.name, formatVersion(e.firmwareVersion).c_str(),
                  (unsigned long)ageMs, e.rssi);
  }
#endif
}

void SerialConsole::handleCommand(const String& cmd) {
  if (cmd.length() == 0) return;
  if (cmd == "paint:on") {
    paint_.setPaintMode(true);
    Serial.println("[wisp.cmd] paint mode ON");
  } else if (cmd == "paint:off") {
    paint_.setPaintMode(false);
    Serial.println("[wisp.cmd] paint mode OFF");
  } else if (cmd == "src:off" || cmd == "src:manual" || cmd == "src:osc") {
    const WispSourceMode m =
        cmd.endsWith("off")    ? WispSourceMode::Off
      : cmd.endsWith("manual") ? WispSourceMode::Manual
                               : WispSourceMode::Osc;
    config_.setSourceMode(m);
    onSourceTransition_(m);
    status_.triggerOnChange();
    Serial.printf("[wisp.cmd] source mode = %s\n", cmd.c_str() + 4);
  } else if (cmd.startsWith("osc:role ")) {
    OscRole role = OscRole::Primary;
    if (!oscRoleFromName(cmd.c_str() + 9, role)) {
      Serial.println(
          "[wisp.cmd] usage: osc:role <primary|secondary|tertiary|fill>");
      return;
    }
    config_.setOscRole(role);
    status_.triggerOnChange();
    Serial.printf("[wisp.cmd] osc role = %s (%s)\n", oscRoleName(role),
                  oscPaletteAddress(role));
  } else if (cmd == "stage:on") {
    stage_.refreshAdvert();
  } else if (cmd == "stage:off") {
    stage_.stop();
  } else if (cmd == "wifi:show") {
    Serial.printf("[wifi] ssid='%s' connected=%d ip=%s\n",
                  wifi_.ssid().c_str(), wifi_.isConnected() ? 1 : 0,
                  WiFi.localIP().toString().c_str());
  } else if (cmd == "eth:show") {
    eth::printStatus(Serial);
  } else if (cmd == "wifi:retry") {
    wifi_.reconnect();
    Serial.println("[wisp.cmd] wifi retry: reconnecting with stored creds");
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
    // Format: "wifi:set <ssid> <pass>". Split on first space after prefix.
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
