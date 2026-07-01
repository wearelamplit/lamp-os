#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include <cstring>

#include "CurrentPalette.h"
#include "LampInventory.h"
#include "MeshLink.h"
#include "PaintDistributor.h"
#include "StatusBeacon.h"
#include "WispRoster.h"
#include "StatusRing.h"
#include "WispConfig.h"
#include "WispOpDispatcher.h"
#include "WispZoneSelector.h"
#include "aurora/AuroraPaletteClient.h"
#include "lamp_protocol.hpp"

#include <WiFi.h>
#include <esp_wifi.h>

#include "ArtnetEmitter.h"
#include "StageBeacon.h"
#include "WifiLink.h"

namespace {

wisp::MeshLink mesh;
wisp::LampInventory inventory;
wisp::CurrentPalette currentPalette;
wisp::PaintDistributor paintDistributor;
wisp::StatusBeacon statusBeacon;
wisp::WispRoster wispRoster;
AuroraPaletteClient auroraClient;
wisp::WifiLink wifi;
wisp::StageBeacon stageBeacon;
wisp::ArtnetEmitter artnetEmitter;

// GPIO 1 (D1): D0 = GPIO 0 = BOOT strap pin; leaving it free keeps
// USB-recover (download mode) working without unplugging the strip.
constexpr uint8_t  kTestStripPin        = 1;
Adafruit_NeoPixel testStrip(wisp::kStatusRingPixelCount, kTestStripPin,
                            NEO_GRB + NEO_KHZ800);
// WS2812 at full power is dazzling at desk distance; 40/255 ≈ 16% reads clearly.
constexpr uint8_t  kStatusRingBrightness = 40;

wisp::WispConfig wispConfig;
wisp::WispOpDispatcher wispOpDispatcher(wispConfig);

// Gossip relay delivers CONTROL_OP multiple times by design; 64-slot ring
// keyed on (sourceMac, msgType, seq) drops re-arrivals before the dispatcher.
lamp_protocol::DedupRing controlOpDedup;

wisp::ZoneSelector zoneSelector;

String serialLineBuf;

// MSG_CONTROL_OP recv handler fires on the WiFi task (Core 0). ArduinoJson
// and Preferences are not safe there; fixed-size memcpy under portMUX,
// drain in loop().
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t pendingWispOpBuf[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
uint16_t pendingWispOpLen = 0;
bool pendingWispOpValid = false;

// Recv-task safe: bounded memcpy + flag under portMUX. If a previous op is
// still pending, the new one wins — single-slot, latest intent matters most.
void postPendingWispOp(const uint8_t* payload, uint16_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return;
  portENTER_CRITICAL(&pendingMux);
  std::memcpy(pendingWispOpBuf, payload, payloadLen);
  pendingWispOpLen = payloadLen;
  pendingWispOpValid = true;
  portEXIT_CRITICAL(&pendingMux);
}

// Empty palette deliberately skips the update so flipping Manual → empty
// doesn't zero the lamps' fallback color.
void pushManualPaletteToCurrent() {
  const auto& cols = wispConfig.manualPalette();
  if (cols.empty()) return;
  Palette p;
  p.id = "manual";
  p.hexColors.reserve(cols.size());
  for (const auto& c : cols) {
    const uint64_t packed = (static_cast<uint64_t>(c.r) << 16) |
                            (static_cast<uint64_t>(c.g) << 8)  |
                            (static_cast<uint64_t>(c.b));
    p.hexColors.push_back(packed);
  }
  currentPalette.update(p, millis());
  paintDistributor.onPaletteChanged();
}

// Event-driven; never called from loop(). Stack-only — no allocation.
void renderRing() {
  uint8_t stopsRgb[wisp::kManualPaletteMaxColors * 3];
  size_t numStops = 0;
  uint8_t pixels[wisp::kStatusRingPixelCount * 3];

  const wisp::WispSourceMode mode = wispConfig.sourceMode();
  // Render as Off when Aurora stream is down, not stale palette.
  const bool auroraLive =
      mode == wisp::WispSourceMode::Aurora && auroraClient.isStreaming();
  if (mode == wisp::WispSourceMode::Manual) {
    const auto& cols = wispConfig.manualPalette();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      stopsRgb[i * 3 + 0] = cols[i].r;
      stopsRgb[i * 3 + 1] = cols[i].g;
      stopsRgb[i * 3 + 2] = cols[i].b;
    }
    numStops = n;
  } else if (auroraLive) {
    const auto& cols = currentPalette.colors();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      // WS2812 ring has no W channel; fold W into R/G with warm bias.
      wisp::rgbwToRgbWarmBias(cols[i].r, cols[i].g, cols[i].b, cols[i].w,
                              stopsRgb[i * 3 + 0], stopsRgb[i * 3 + 1],
                              stopsRgb[i * 3 + 2]);
    }
    numStops = n;
  }
  // Off and Aurora-with-no-stream show the operator off-color.
  // Manual with empty palette falls through to warm-white.
  if (mode == wisp::WispSourceMode::Off ||
      (mode == wisp::WispSourceMode::Aurora && !auroraLive)) {
    const wisp::ManualPaletteColor offColor = wispConfig.offColor();
    for (size_t i = 0; i < wisp::kStatusRingPixelCount; ++i) {
      pixels[i * 3 + 0] = offColor.r;
      pixels[i * 3 + 1] = offColor.g;
      pixels[i * 3 + 2] = offColor.b;
    }
  } else if (!wisp::computeRingGradient(stopsRgb, numStops, pixels,
                                        wisp::kStatusRingPixelCount)) {
    wisp::fillRingWarmWhite(pixels, wisp::kStatusRingPixelCount);
  }

  for (size_t i = 0; i < wisp::kStatusRingPixelCount; ++i) {
    testStrip.setPixelColor(static_cast<uint16_t>(i),
                            pixels[i * 3 + 0],
                            pixels[i * 3 + 1],
                            pixels[i * 3 + 2]);
  }
  testStrip.show();
}

// Idempotent: safe to call at boot (from NVS) and on every mode flip.
void applySourceModeTransition(wisp::WispSourceMode mode) {
  switch (mode) {
    case wisp::WispSourceMode::Off:
      paintDistributor.setPaintMode(false);
      Serial.println("[wisp] source=Off — broadcast RESTORE; paintMode off");
      break;
    case wisp::WispSourceMode::Manual:
      paintDistributor.setPaintMode(true);
      pushManualPaletteToCurrent();
      Serial.println("[wisp] source=Manual — using stored manual palette");
      break;
    case wisp::WispSourceMode::Aurora:
      // Clear stale palette; onAuroraPalette enables paint when a live
      // palette arrives; loop's liveness check disables it if stream drops.
      currentPalette.clear();
      paintDistributor.setPaintMode(auroraClient.isStreaming());
      Serial.printf("[wisp] source=Aurora — %s\n",
                    auroraClient.isStreaming() ? "stream live"
                                               : "no stream, holding off");
      break;
  }
  renderRing();
}

void drainPendingWispOp() {
  uint8_t localBuf[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t localLen = 0;
  bool have = false;
  portENTER_CRITICAL(&pendingMux);
  if (pendingWispOpValid) {
    std::memcpy(localBuf, pendingWispOpBuf, pendingWispOpLen);
    localLen = pendingWispOpLen;
    pendingWispOpValid = false;
    pendingWispOpLen = 0;
    have = true;
  }
  portEXIT_CRITICAL(&pendingMux);
  if (!have) return;

  wisp::DispatchResult res = wispOpDispatcher.dispatch(localBuf, localLen);
  switch (res) {
    case wisp::DispatchResult::AppliedSourceChange: {
      applySourceModeTransition(wispConfig.sourceMode());
      statusBeacon.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedManualPalette: {
      if (wispConfig.sourceMode() == wisp::WispSourceMode::Manual) {
        pushManualPaletteToCurrent();
        renderRing();
      }
      statusBeacon.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedZoneChange: {
      if (wispConfig.hasSelectedZone()) {
        const int newZone = wispConfig.selectedZone();
        zoneSelector.setFromOp(newZone);
        Serial.printf("[wisp] zone set by app op to %d (source=appOp)\n",
                      newZone);
      } else {
        zoneSelector.clearFromOp();
        Serial.println("[wisp] zone cleared by app op (source=none)");
      }
      statusBeacon.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedWifiChange:
      Serial.println("[wisp] wifi creds updated; STA reconnect + advert refresh kicked");
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedOffColor:
      if (wispConfig.sourceMode() == wisp::WispSourceMode::Off) {
        renderRing();
      }
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedShuffle:
      paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());
      paintDistributor.onPaletteChanged();
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::Ignored:
    case wisp::DispatchResult::Malformed:
      break;
  }
}

// Fires on the WiFi task — protocol parse + bounded memcpy only.
// No logging, no Preferences, no ArduinoJson.
void onMeshPacket(const uint8_t* /*srcMac*/, const uint8_t* data, size_t len,
                  int8_t rssi) {
  const uint8_t msgType = lamp_protocol::inspect(data, len);
  if (msgType == lamp_protocol::MSG_HELLO) {
    lamp_protocol::ParsedHello h;
    if (!lamp_protocol::parseHello(data, len, h)) return;
    const std::string peerName =
        h.nameLen ? std::string(h.name, h.nameLen) : std::string();
    inventory.recordHello(h.sourceMac, peerName, h.base, h.shade,
                          h.firmwareVersion, millis(), rssi);
    return;
  }
  if (msgType == lamp_protocol::MSG_WISP_CLAIM) {
    lamp_protocol::ParsedWispClaim wc;
    if (!lamp_protocol::parseWispClaim(data, len, wc)) return;
    wispRoster.recordPeerClaim(wc.sourceMac, wc.entries, wc.count, millis());
    return;
  }
  if (msgType == lamp_protocol::MSG_CONTROL_OP) {
    lamp_protocol::ParsedControlOp op;
    if (!lamp_protocol::parseControlOp(data, len, op)) return;
    // Dedup before post: gossip duplicate must not displace a pending fresh op.
    if (!controlOpDedup.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP,
                               op.seq)) {
      return;
    }
    postPendingWispOp(op.payload, op.payloadLen);
    return;
  }
}

// Runs from auroraClient.loop() on the main task.
void onAuroraPalette(int zone, const Palette& p) {
  zoneSelector.observe(zone);

  // First-seen latch: only when neither NVS nor an app op has chosen a zone.
  if (zoneSelector.latchFirstSeen(zone)) {
    Serial.printf("[wisp] claimed Aurora zone %d (source=firstSeen)\n", zone);
    statusBeacon.triggerOnChange();
  }

  if (zone != zoneSelector.currentZone()) {
    Serial.printf("[wisp] ignoring zone %d palette (selected %d, source=%s)\n",
                  zone, zoneSelector.currentZone(),
                  wisp::zoneSourceName(zoneSelector.source()));
    return;
  }

  // Only Aurora mode lets these callbacks drive CurrentPalette; Manual/Off
  // would have their palette overwritten by the next Aurora notify.
  if (wispConfig.sourceMode() != wisp::WispSourceMode::Aurora) {
    return;
  }

  currentPalette.update(p, millis());
  const auto& cols = currentPalette.colors();
  Serial.printf("[wisp] palette change: %s with %u colors\n",
                currentPalette.paletteId().c_str(),
                (unsigned)cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    Serial.printf("  [%u] r=%u g=%u b=%u w=%u\n",
                  (unsigned)i, cols[i].r, cols[i].g, cols[i].b, cols[i].w);
  }
  paintDistributor.setPaintMode(true);
  artnetEmitter.onPaletteChanged();
  renderRing();
}

void processSerialCommand(const String& cmd) {
  if (cmd.length() == 0) return;
  if (cmd == "paint:on") {
    paintDistributor.setPaintMode(true);
    Serial.println("[wisp.cmd] paint mode ON");
  } else if (cmd == "paint:off") {
    paintDistributor.setPaintMode(false);
    Serial.println("[wisp.cmd] paint mode OFF");
  } else if (cmd == "src:off" || cmd == "src:manual" || cmd == "src:aurora") {
    const wisp::WispSourceMode m =
        cmd.endsWith("off")    ? wisp::WispSourceMode::Off
      : cmd.endsWith("manual") ? wisp::WispSourceMode::Manual
                               : wisp::WispSourceMode::Aurora;
    wispConfig.setSourceMode(m);
    applySourceModeTransition(m);
    statusBeacon.triggerOnChange();
    Serial.printf("[wisp.cmd] source mode = %s\n", cmd.c_str() + 4);
  } else if (cmd == "artnet:on") {
    artnetEmitter.setEnabled(true);
  } else if (cmd == "artnet:off") {
    artnetEmitter.setEnabled(false);
  } else if (cmd == "stage:on") {
    stageBeacon.refreshAdvert();
  } else if (cmd == "stage:off") {
    stageBeacon.stop();
  } else if (cmd == "wifi:show") {
    Serial.printf("[wifi] ssid='%s' connected=%d ip=%s\n",
                  wifi.ssid().c_str(), wifi.isConnected() ? 1 : 0,
                  WiFi.localIP().toString().c_str());
  } else if (cmd == "wifi:clear") {
    // WiFi.disconnect alone doesn't reset the radio channel; need
    // esp_wifi_set_channel to snap back to LAMP_ESPNOW_CHANNEL.
    wispConfig.setWifi("", "");
    wifi.reconnect();
    esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (stageBeacon.isAdvertising()) {
      stageBeacon.refreshAdvert();
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
    wispConfig.setWifi(ssid, pass);
    wifi.reconnect();
    if (stageBeacon.isAdvertising()) {
      stageBeacon.refreshAdvert();
    }
    Serial.println("[wisp.cmd] wifi creds saved");
  } else {
    Serial.printf("[wisp.cmd] unknown command: %s\n", cmd.c_str());
  }
}

void pumpSerial() {
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\r') continue;  // strip CR; macOS / Linux send LF only
    if (ch == '\n') {
      String cmd = serialLineBuf;
      cmd.trim();
      serialLineBuf = String();
      processSerialCommand(cmd);
      continue;
    }
    if (serialLineBuf.length() < 128) serialLineBuf += static_cast<char>(ch);
  }
}

String formatVersion(uint32_t v) {
  uint8_t major = (v >> 16) & 0xFF;
  uint8_t minor = (v >> 8) & 0xFF;
  uint8_t patch = v & 0xFF;
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
  return String(buf);
}

void dumpInventory(uint32_t /*nowMs*/) {
  // Re-sample: HELLOs can arrive during auroraClient.loop(); a stale nowMs
  // causes unsigned-subtraction wraparound on (nowMs - lastSeenMs).
  const uint32_t nowMs = millis();
  auto roster = inventory.snapshot();
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
                zoneSelector.currentZone(),
                wisp::zoneSourceName(zoneSelector.source()),
                (unsigned)zoneSelector.observedCount());
}

// Stable across reboots; Aurora uses it to recognize returning subscribers.
String buildInstanceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "wisp-%06lx",
           (unsigned long)(mac & 0xFFFFFFul));
  return String(buf);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // USB-CDC needs a moment after USB enumerate before printf is ready.
  delay(200);
  Serial.println("wisp: boot");

  // First renderRing() is deferred until after wispConfig.begin() so the
  // cached sourceMode drives the layout.
  testStrip.begin();
  testStrip.setBrightness(kStatusRingBrightness);
  testStrip.clear();
  testStrip.show();
  Serial.printf("[wisp.ring] %u pixels on GPIO %u (brightness=%u/255)\n",
                (unsigned)wisp::kStatusRingPixelCount,
                (unsigned)kTestStripPin,
                (unsigned)kStatusRingBrightness);

  wispConfig.begin();
  if (wispConfig.hasSelectedZone()) {
    const int z = wispConfig.selectedZone();
    zoneSelector.setFromNvs(z);
    Serial.printf("[wisp] zone %d from NVS\n", z);
  } else {
    Serial.println("[wisp] no zone in NVS; will latch first-seen Aurora zone");
  }

  mesh.onPacket(onMeshPacket);
  if (!mesh.begin()) {
    Serial.println("[wisp] mesh init failed; will retry in 5s");
  }

  wifi.begin(&wispConfig);
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  artnetEmitter.begin(&currentPalette, &wifi);
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);

  auroraClient.setInstanceId(buildInstanceId().c_str());
  auroraClient.onActivePalette(onAuroraPalette);
  auroraClient.onZoneObserved([](int zone) { zoneSelector.observe(zone); });
  auroraClient.begin();
  Serial.printf("[wisp] aurora client started as %s\n",
                buildInstanceId().c_str());

  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);

  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);
  paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());

  // carriedFw* zero-fill; wire layout retained for back-compat with older lamps.
  statusBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                     &zoneSelector, &auroraClient, &wispConfig, &wispRoster);
  statusBeacon.startTimer();

  applySourceModeTransition(wispConfig.sourceMode());

  Serial.println("[wisp] paint distributor + status beacon online");
  Serial.println("[wisp] cmds: paint:on/off  artnet:on/off  stage:on/off");
  Serial.println("[wisp] cmds: src:off/manual/aurora  wifi:set <ssid> <pass>  wifi:show");
}

void loop() {
  static uint32_t lastDumpMs = 0;
  static uint32_t lastPruneMs = 0;
  const uint32_t now = millis();

  auroraClient.loop();
  pumpSerial();
  drainPendingWispOp();
  {
    auto inv = inventory.snapshot();
    wisp::WispRoster::LampObservation obs[wisp::WISP_ROSTER_MAX_LAMPS];
    size_t n = 0;
    for (const auto& e : inv) {
      if (n >= wisp::WISP_ROSTER_MAX_LAMPS) break;
      std::memcpy(obs[n].mac, e.mac, 6);
      obs[n].rssi = e.rssi;
      n++;
    }
    wispRoster.recomputeClaims(obs, n, now);
  }
  // Edge-triggered: stream drop → RESTORE walk; onAuroraPalette re-enables
  // paint when a fresh palette arrives.
  static bool auroraWasStreaming = false;
  if (wispConfig.sourceMode() == wisp::WispSourceMode::Aurora) {
    const bool streaming = auroraClient.isStreaming();
    if (auroraWasStreaming && !streaming) {
      currentPalette.clear();
      paintDistributor.setPaintMode(false);
      renderRing();
      statusBeacon.triggerOnChange();
      Serial.println("[wisp] Aurora stream dropped — holding off");
    }
    auroraWasStreaming = streaming;
  }

  paintDistributor.tick(now);
  artnetEmitter.tick(now);

  if (now - lastDumpMs > 10000) {
    lastDumpMs = now;
    dumpInventory(now);
  }

  if (now - lastPruneMs > 30000) {
    lastPruneMs = now;
    inventory.prune(now, LAMP_PRUNE_TIME_MS);
  }

  delay(5);
}
