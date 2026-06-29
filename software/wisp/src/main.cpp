// wisp — palette bridge + maintenance carrier.
//
// Zone selection comes from three places, in priority order:
//   (1) NVS-persisted choice from a prior setZone op (WispConfig);
//   (2) first-seen Aurora zone latch (default so an unconfigured wisp still
//       does something out of the box);
//   (3) a runtime setZone op from the Flutter app pane (BLE → mesh →
//       MSG_CONTROL_OP → here), write-through into NVS.
// observedZones tracks every zone advertising on the Aurora bus (even
// unresolved) so the app pane can offer the full list.

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

// 30-pixel NeoPixel ring on GPIO 1 (D1 on the Xiao C6). Live indicator of
// the current source/palette state (Off/Manual/Aurora). D1 over D0 because
// GPIO 0 is the BOOT strap pin: leaving it free keeps USB-recover (download
// mode) working without unplugging the strip. D1 has no shared peripheral
// function on the C6. Update is event-driven (renderRing()).
constexpr uint8_t  kTestStripPin        = 1;
Adafruit_NeoPixel testStrip(wisp::kStatusRingPixelCount, kTestStripPin,
                            NEO_GRB + NEO_KHZ800);
// Ring brightness scale. WS2812 at full power is dazzling at desk distance;
// 40/255 ≈ 16% reads as a clear indicator without overwhelming the room.
constexpr uint8_t  kStatusRingBrightness = 40;

wisp::WispConfig wispConfig;
wisp::WispOpDispatcher wispOpDispatcher(wispConfig);

// Dedup ring for MSG_CONTROL_OP. The wisp receives CONTROL_OP frames off the
// gossip-relay mesh, so the same op arrives multiple times by design (sender
// + 1-hop relays); the ring (keyed on sourceMac+seq) drops re-arrivals before
// the dispatcher sees them.
lamp_protocol::DedupRing controlOpDedup;

wisp::ZoneSelector zoneSelector;

// Bench-only serial command buffer. The runtime BLE proxy
// (MSG_WISP_OP from the app pane) is the production path.
String serialLineBuf;

// Pending wispOp slot (recv-task → loop-task hand-off). The MSG_CONTROL_OP
// recv handler fires on the WiFi recv task; running ArduinoJson or touching
// Preferences there stalls the radio and drops subsequent ESP-NOW frames.
// Fixed-size memcpy under a portMUX, drain in loop().
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
// CONTROL_OP payloads are bounded by CONTROL_MAX_PAYLOAD; using that as the
// slot size means a worst-case op fits without allocating.
uint8_t pendingWispOpBuf[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
uint16_t pendingWispOpLen = 0;
bool pendingWispOpValid = false;

// Recv-task safe: bounded memcpy + flag flip under portMUX, no heap, no
// logging. If a previous op is still pending the new one wins (single-slot,
// latest intent matters most).
void postPendingWispOp(const uint8_t* payload, uint16_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return;
  portENTER_CRITICAL(&pendingMux);
  std::memcpy(pendingWispOpBuf, payload, payloadLen);
  pendingWispOpLen = payloadLen;
  pendingWispOpValid = true;
  portEXIT_CRITICAL(&pendingMux);
}

// Push the manual palette through CurrentPalette so PaintDistributor's
// existing path paints it. Empty palette skips the update deliberately:
// CurrentPalette keeps its prior contents so flipping Manual → empty
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

// Render the current source/palette state to the ring. Event-driven (called
// on each state transition + once at boot), never from loop(). No heap:
// stops + pixels are fixed-size stack buffers.
void renderRing() {
  uint8_t stopsRgb[wisp::kManualPaletteMaxColors * 3];
  size_t numStops = 0;
  uint8_t pixels[wisp::kStatusRingPixelCount * 3];

  const wisp::WispSourceMode mode = wispConfig.sourceMode();
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
  } else if (mode == wisp::WispSourceMode::Aurora) {
    const auto& cols = currentPalette.colors();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      // Aurora carries an RGBW sample; the WS2812 ring has no W channel.
      // Fold W into R/G with a warm bias so a near-white Aurora palette
      // doesn't read as blue-white on the indicator.
      wisp::rgbwToRgbWarmBias(cols[i].r, cols[i].g, cols[i].b, cols[i].w,
                              stopsRgb[i * 3 + 0], stopsRgb[i * 3 + 1],
                              stopsRgb[i * 3 + 2]);
    }
    numStops = n;
  }
  // numStops == 0 here means Off or empty palette. Off shows the
  // operator-chosen color; empty Manual/Aurora fall to warm-white.
  if (mode == wisp::WispSourceMode::Off) {
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

// Apply the side-effects of a source-mode transition. Called after the
// dispatcher persists a new mode, and once at boot so the NVS mode drives
// behavior. Idempotent.
void applySourceModeTransition(wisp::WispSourceMode mode) {
  switch (mode) {
    case wisp::WispSourceMode::Off:
      // setPaintMode(false) broadcasts RESTORE to every peer and stops the
      // backstop refresh.
      paintDistributor.setPaintMode(false);
      Serial.println("[wisp] source=Off — broadcast RESTORE; paintMode off");
      break;
    case wisp::WispSourceMode::Manual:
      paintDistributor.setPaintMode(true);
      pushManualPaletteToCurrent();
      Serial.println("[wisp] source=Manual — using stored manual palette");
      break;
    case wisp::WispSourceMode::Aurora:
      paintDistributor.setPaintMode(true);
      Serial.println("[wisp] source=Aurora — awaiting Aurora callback");
      break;
  }
  renderRing();
}

// Loop-task: copy out under portMUX, then dispatch on a local buffer so the
// portMUX critical section stays short.
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
      // In Manual, push the new palette through immediately so lamps repaint
      // without a mode flip. In Off/Aurora it stays persisted-only;
      // CurrentPalette belongs to Aurora's callback there.
      if (wispConfig.sourceMode() == wisp::WispSourceMode::Manual) {
        pushManualPaletteToCurrent();
        // Ring reflects the manual palette only in Manual; in Off/Aurora the
        // persisted palette is background state.
        renderRing();
      }
      statusBeacon.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedZoneChange: {
      // Reconcile ZoneSelector with WispConfig. If the op set a zone, latch
      // it as AppOp-sourced; if it cleared, drop back to None and let the
      // next first-seen latch take over.
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
      // Dispatcher already persisted creds + kicked reconnect/refreshAdvert.
      // Only the wispStatus broadcast remains so the app sees the
      // wifiConnected transition without waiting for the 30s heartbeat.
      Serial.println("[wisp] wifi creds updated; STA reconnect + advert refresh kicked");
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedOffColor:
      // Off-mode ring color stored. Repaint only if currently in Off;
      // otherwise it's background state until the next Off transition.
      if (wispConfig.sourceMode() == wisp::WispSourceMode::Off) {
        renderRing();
      }
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::Ignored:
    case wisp::DispatchResult::Malformed:
      // Nothing to do; dispatcher already logged what mattered.
      break;
  }
}

// HELLO + CONTROL_OP recv handler. Fires on the WiFi task; keep it tight:
// protocol parse + bounded memcpy only, no logging/Preferences/ArduinoJson.
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
    // Peer wisp's claim broadcast (also gossip-relayed by lamps for wisps
    // out of direct range). Stash into WispRoster; claim computation runs on
    // the loop task.
    lamp_protocol::ParsedWispClaim wc;
    if (!lamp_protocol::parseWispClaim(data, len, wc)) return;
    wispRoster.recordPeerClaim(wc.sourceMac, wc.entries, wc.count, millis());
    return;
  }
  if (msgType == lamp_protocol::MSG_CONTROL_OP) {
    lamp_protocol::ParsedControlOp op;
    if (!lamp_protocol::parseControlOp(data, len, op)) return;
    // Dedup BEFORE post: a gossip-relayed duplicate must not displace a
    // pending fresh op. Per-msgType ring keyed on sourceMac+seq.
    if (!controlOpDedup.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP,
                               op.seq)) {
      return;
    }
    postPendingWispOp(op.payload, op.payloadLen);
    return;
  }
}

// Aurora palette callback. Runs from auroraClient.loop() on the main task.
void onAuroraPalette(int zone, const Palette& p) {
  // observe() also runs via onZoneObserved_ on every announcement; recording
  // here too is safe if a resolve outpaces the announce path.
  zoneSelector.observe(zone);

  // First-seen latch: only when neither NVS nor an app op has chosen a zone.
  if (zoneSelector.latchFirstSeen(zone)) {
    Serial.printf("[wisp] claimed Aurora zone %d (source=firstSeen)\n", zone);
    // Zone change: fan out so the app pane shows the picked zone.
    statusBeacon.triggerOnChange();
  }

  if (zone != zoneSelector.currentZone()) {
    Serial.printf("[wisp] ignoring zone %d palette (selected %d, source=%s)\n",
                  zone, zoneSelector.currentZone(),
                  wisp::zoneSourceName(zoneSelector.source()));
    return;
  }

  // Only Aurora mode lets Aurora callbacks drive CurrentPalette. Skipping in
  // Manual/Off keeps onPaletteChanged() quiet so a manual palette isn't
  // bulldozed on the next Aurora notify.
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
  paintDistributor.onPaletteChanged();
  artnetEmitter.onPaletteChanged();
  renderRing();
}

// Serial command handler (bench/debug). One stripped line at a time.
void processSerialCommand(const String& cmd) {
  if (cmd.length() == 0) return;
  if (cmd == "paint:on") {
    paintDistributor.setPaintMode(true);
    Serial.println("[wisp.cmd] paint mode ON");
  } else if (cmd == "paint:off") {
    paintDistributor.setPaintMode(false);
    Serial.println("[wisp.cmd] paint mode OFF");
  } else if (cmd == "artnet:on") {
    artnetEmitter.setEnabled(true);
  } else if (cmd == "artnet:off") {
    artnetEmitter.setEnabled(false);
  } else if (cmd == "stage:on") {
    // Re-reads creds from WispConfig and starts advertising; stops if creds
    // are empty.
    stageBeacon.refreshAdvert();
  } else if (cmd == "stage:off") {
    stageBeacon.stop();
  } else if (cmd == "wifi:show") {
    Serial.printf("[wifi] ssid='%s' connected=%d ip=%s\n",
                  wifi.ssid().c_str(), wifi.isConnected() ? 1 : 0,
                  WiFi.localIP().toString().c_str());
  } else if (cmd == "wifi:clear") {
    // Drop creds + disconnect + re-pin the radio to LAMP_ESPNOW_CHANNEL.
    // Once associated to a venue AP the radio sits on the AP's channel and
    // mesh broadcasts miss peers; WiFi.disconnect alone doesn't reset it,
    // esp_wifi_set_channel snaps it back.
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
    // Route through WispConfig (same source-of-truth as the setWifi op), then
    // kick the two downstream consumers so serial and BLE-op paths converge.
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

// Drain the Serial RX FIFO into serialLineBuf, dispatching on newline. Lives
// in loop() (no dedicated task needed); HELLO emission is on a FreeRTOS timer
// so loop() pacing here is fine.
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

// Decode a packed semver back into a human string for the serial dump.
String formatVersion(uint32_t v) {
  uint8_t major = (v >> 16) & 0xFF;
  uint8_t minor = (v >> 8) & 0xFF;
  uint8_t patch = v & 0xFF;
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
  return String(buf);
}

void dumpInventory(uint32_t /*nowMs*/) {
  // Re-sample millis() rather than trusting the loop-top nowMs: HELLOs can
  // arrive between the loop-top sample and here (auroraClient.loop() can run
  // hundreds of ms), and a lastSeenMs fresher than nowMs wraps the unsigned
  // subtraction into a bogus age.
  const uint32_t nowMs = millis();
  auto roster = inventory.snapshot();
  Serial.printf("[wisp] roster (%u lamp%s):\n",
                (unsigned)roster.size(), roster.size() == 1 ? "" : "s");
  for (const auto& e : roster) {
    // Clamp if lastSeenMs is ahead of now (millis() overflow boundary)
    // instead of wrapping.
    const uint32_t ageMs = (nowMs >= e.lastSeenMs) ? nowMs - e.lastSeenMs : 0;
    Serial.printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-12s  fw=%s  age=%lums\n",
                  e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
                  e.name.c_str(), formatVersion(e.firmwareVersion).c_str(),
                  (unsigned long)ageMs);
  }
  Serial.printf("[wisp] zone=%d source=%s observed=%u\n",
                zoneSelector.currentZone(),
                wisp::zoneSourceName(zoneSelector.source()),
                (unsigned)zoneSelector.observed().size());
}

// Stable instance id from the chip MAC's low 24 bits. Aurora uses it to
// recognize a returning subscriber, so it must be consistent across reboots.
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
  // ESP32-C6 USB-CDC takes a moment after USB enumerate to be ready for
  // printf; small delay keeps the boot banner from getting swallowed.
  delay(200);
  Serial.println("wisp: boot");

  // Bring the strip up at the brightness scale and show warm-white. First
  // real render happens after wispConfig.begin() so sourceMode drives it.
  testStrip.begin();
  testStrip.setBrightness(kStatusRingBrightness);
  testStrip.clear();
  testStrip.show();
  Serial.printf("[wisp.ring] %u pixels on GPIO %u (brightness=%u/255)\n",
                (unsigned)wisp::kStatusRingPixelCount,
                (unsigned)kTestStripPin,
                (unsigned)kStatusRingBrightness);

  // Bring NVS up before anything reads selZone; ZoneSelector seeds from the
  // cached value here.
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

  // ArtNet bridge. WifiLink + StageBeacon read creds from the shared
  // WispConfig store and re-read on reconnect()/refreshAdvert().
  // StageBeacon + ArtnetEmitter are gated off until an explicit stage:on /
  // artnet:on command (or a setWifi op for the stage advert).
  wifi.begin(&wispConfig);
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  artnetEmitter.begin(&currentPalette, &wifi);
  // Hand the dispatcher the sinks for chaining a setWifi op into STA
  // reconnect + advert refresh. main.cpp owns the globals; dispatcher borrows.
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);

  auroraClient.setInstanceId(buildInstanceId().c_str());
  auroraClient.onActivePalette(onAuroraPalette);
  // Capture every announced zone, not just ones whose palettes resolve.
  // Fires on the main task from auroraClient.loop().
  auroraClient.onZoneObserved([](int zone) { zoneSelector.observe(zone); });
  auroraClient.begin();
  Serial.printf("[wisp] aurora client started as %s\n",
                buildInstanceId().c_str());

  // Multi-wisp coordination. WispRoster owns the peer-claim view +
  // claim-decision logic; PaintDistributor filters its walk by it;
  // StatusBeacon broadcasts claims every 2s. Self-MAC drives the lower-MAC
  // tiebreaker.
  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);

  // Status beacon broadcasts MSG_WISP_HELLO every 2s on a FreeRTOS timer so
  // cadence survives Aurora loop() stalls.
  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);

  // Wisp doesn't participate in OTA (lamps gossip firmware peer-to-peer).
  // MSG_WISP_HELLO's carriedFw* fields zero-fill; wire layout retained for
  // back-compat with older lamps.
  statusBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                     &zoneSelector, &auroraClient, &wispConfig, &wispRoster);
  statusBeacon.startTimer();

  // Apply the persisted source mode now that everything it touches is up.
  // Makes a Manual-mode wisp paint from the stored palette on boot without
  // waiting for an app connection.
  applySourceModeTransition(wispConfig.sourceMode());

  Serial.println("[wisp] paint distributor + status beacon online");
  Serial.println("[wisp] cmds: paint:on/off  artnet:on/off  stage:on/off");
  Serial.println("[wisp] cmds: wifi:set <ssid> <pass>  wifi:show");
}

void loop() {
  static uint32_t lastDumpMs = 0;
  static uint32_t lastPruneMs = 0;
  const uint32_t now = millis();

  auroraClient.loop();
  pumpSerial();
  drainPendingWispOp();
  // Recompute the claim set from the peer-RSSI view + inventory before the
  // paint walk so PaintDistributor's filter sees the latest decisions.
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
  paintDistributor.tick(now);
  artnetEmitter.tick(now);

  if (now - lastDumpMs > 10000) {
    lastDumpMs = now;
    dumpInventory(now);
  }

  // Prune at half the prune window so a dropped lamp clears within ~one extra
  // dump cycle.
  if (now - lastPruneMs > 30000) {
    lastPruneMs = now;
    inventory.prune(now, LAMP_PRUNE_TIME_MS);
  }

  delay(5);
}
