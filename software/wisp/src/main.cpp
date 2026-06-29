// wisp — palette bridge + maintenance carrier.
//
// Zone-selection. The wisp is a MSG_CONTROL_OP receiver. Selection
// can come from three places, in priority order:
//   (1) NVS-persisted choice from a prior `setZone` op (WispConfig);
//   (2) first-seen Aurora zone latch (legacy default, kept so an unconfigured
//       wisp still does something useful out of the box);
//   (3) a runtime `setZone` op from the Flutter app pane (BLE → mesh →
//       MSG_CONTROL_OP → here), which write-throughs into NVS.
//
// The wisp also keeps an `observedZones` set so the app pane can offer the
// list of zones currently advertising on the Aurora bus, even ones whose
// palettes haven't resolved yet.

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

// 30-pixel NeoPixel ring on GPIO 1 (D1 on the Xiao C6). Live status
// indicator that mirrors the current source/palette state:
//   Off    → warm-white fill (tungsten bulb look)
//   Manual → manualPalette() stretched left→right with gradient interp
//   Aurora → CurrentPalette stretched left→right with gradient interp
// D1 was picked over D0 because GPIO 0 is the BOOT strap pin — leaving
// it free means USB-recover (download mode) still works without
// unplugging the strip. D1 has no shared peripheral function on the
// C6 variant. Update is event-driven (renderRing()), not loop-paced.
constexpr uint8_t  kTestStripPin        = 1;
Adafruit_NeoPixel testStrip(wisp::kStatusRingPixelCount, kTestStripPin,
                            NEO_GRB + NEO_KHZ800);
// Global brightness scale for the ring. WS2812 at full power is dazzling
// at desk distance; 40/255 ≈ 16% reads as a clear indicator without
// overwhelming the room. Tunable later via serial cmd if needed.
constexpr uint8_t  kStatusRingBrightness = 40;

wisp::WispConfig wispConfig;
wisp::WispOpDispatcher wispOpDispatcher(wispConfig);

// Per-msgType dedup ring for MSG_CONTROL_OP. The wisp now joins the
// gossip-relay mesh as a receiver of CONTROL_OP frames, so the same op will
// reach us multiple times by design (sender + 1-hop relays). The 64-slot
// portMUX-guarded ring keyed on (sourceMac, msgType, seq) drops the
// re-arrivals before they hit the dispatcher.
// Single-purpose ring (CONTROL_OP only) — no need to shard per-msgType like
// ShowReceiver on the lamp side, which juggles HELLO/CONTROL_OP/OVERRIDE/EVENT
// through one queue.
lamp_protocol::DedupRing controlOpDedup;

// ZoneSelector now lives in WispZoneSelector.{h,cpp} (extracted so
// StatusBeacon can read it to emit wispStatus JSON).
wisp::ZoneSelector zoneSelector;

// Bench-only serial command buffer. The runtime BLE proxy
// (MSG_WISP_OP from the app pane) is the production path.
String serialLineBuf;

// --- Pending wispOp slot (recv-task → loop-task hand-off) ----------------
// The MSG_CONTROL_OP recv handler fires on the WiFi recv task (Core 0). We
// can't run ArduinoJson or touch Preferences from there: Preferences writes
// stall the radio, and a long parse window will drop subsequent ESP-NOW
// frames. Mirror the lamp-os pending-slot pattern — fixed-size memcpy under
// a portMUX, drain in loop().
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;
// CONTROL_OP payloads are bounded by CONTROL_MAX_PAYLOAD; using that as the
// slot size means a worst-case op fits without allocating.
uint8_t pendingWispOpBuf[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
uint16_t pendingWispOpLen = 0;
bool pendingWispOpValid = false;

// Recv-task safe: bounded memcpy + flag flip under portMUX. No heap, no
// logging. If a previous op is still pending (drain hasn't run yet) the new
// one wins — single-slot semantics, latest intent matters most.
void postPendingWispOp(const uint8_t* payload, uint16_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return;
  portENTER_CRITICAL(&pendingMux);
  std::memcpy(pendingWispOpBuf, payload, payloadLen);
  pendingWispOpLen = payloadLen;
  pendingWispOpValid = true;
  portEXIT_CRITICAL(&pendingMux);
}

// Push the operator-defined manual palette through CurrentPalette so the
// PaintDistributor's existing painting path (which only knows about
// CurrentPalette) can paint the lamps without any new code path. Caller
// is responsible for tearing or honoring paintMode appropriately; this
// helper is shape-only.
//
// When the manual palette is empty we deliberately skip the update —
// CurrentPalette holds onto its prior contents which lets the operator
// flip Manual → empty without zeroing the lamps' fall-back color.
// PaintDistributor still walks the roster on the on-mode-change kick;
// with no palette the per-peer fan-out will fade-to-nothing, which is
// the same end state.
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

// Push the current source/palette state to the 30-pixel ring. Event-
// driven — called from each spot in main.cpp that already runs on a
// state transition (mode flip, manual palette edit, Aurora callback),
// plus once at boot. NOT called from loop(). The function does no
// allocation: stops + pixels live on the stack in fixed-size buffers
// sized to kManualPaletteMaxColors and kStatusRingPixelCount.
void renderRing() {
  uint8_t stopsRgb[wisp::kManualPaletteMaxColors * 3];
  size_t numStops = 0;
  uint8_t pixels[wisp::kStatusRingPixelCount * 3];

  const wisp::WispSourceMode mode = wispConfig.sourceMode();
  // Aurora is only a live source while its stream is up. With no stream we
  // render as Off rather than whatever stale palette the buffer last held.
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
      // Aurora carries an RGBW sample; the WS2812 ring has no W channel.
      // Fold W into R/G with a warm bias so a near-white Aurora palette
      // doesn't read as blue-white on the indicator.
      wisp::rgbwToRgbWarmBias(cols[i].r, cols[i].g, cols[i].b, cols[i].w,
                              stopsRgb[i * 3 + 0], stopsRgb[i * 3 + 1],
                              stopsRgb[i * 3 + 2]);
    }
    numStops = n;
  }
  // Off — and Aurora with no live stream — show the operator-chosen off
  // color. Manual with an empty palette falls through to warm-white (its
  // config is "missing", not "I chose to be off").
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

// Apply the side-effects of a source-mode transition. Called after the
// dispatcher persists the new mode, and once at boot so the freshly-
// loaded NVS mode actually drives behavior. Idempotent: safe to call
// repeatedly with the same mode.
//
//   Off    → broadcast RESTORE_COLORS to every peer (PaintDistributor's
//            existing setPaintMode(false) path), then hold off.
//   Manual → push the stored palette into CurrentPalette and flip paint
//            mode on. If the palette is empty, the kick still happens
//            but with whatever CurrentPalette already held (Aurora's
//            last value if any) — operator can fix by adding colors.
//   Aurora → flip paint mode on and let onAuroraPalette repopulate
//            CurrentPalette on the next subscription tick.
void applySourceModeTransition(wisp::WispSourceMode mode) {
  switch (mode) {
    case wisp::WispSourceMode::Off:
      // setPaintMode(false) is the existing "broadcast RESTORE to every
      // known peer" path. It also stops the 10s backstop refresh.
      paintDistributor.setPaintMode(false);
      Serial.println("[wisp] source=Off — broadcast RESTORE; paintMode off");
      break;
    case wisp::WispSourceMode::Manual:
      paintDistributor.setPaintMode(true);
      pushManualPaletteToCurrent();
      Serial.println("[wisp] source=Manual — using stored manual palette");
      break;
    case wisp::WispSourceMode::Aurora:
      // Drop any stale (manual) palette so we never paint/ring it while
      // waiting on Aurora. Paint only when the stream is already live;
      // onAuroraPalette turns it on when a palette actually arrives, and the
      // loop's liveness check turns it back off if the stream drops.
      currentPalette.clear();
      paintDistributor.setPaintMode(auroraClient.isStreaming());
      Serial.printf("[wisp] source=Aurora — %s\n",
                    auroraClient.isStreaming() ? "stream live"
                                               : "no stream, holding off");
      break;
  }
  // Reflect the new mode on the indicator ring. Off → warm white,
  // Manual → manual palette, Aurora → whatever CurrentPalette already
  // holds (next Aurora callback will repaint it).
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
      // If we're currently in Manual, push the new palette through right
      // away so the lamps repaint without waiting for a mode flip. In
      // Off / Aurora we still persist the palette (the dispatcher already
      // did) but don't touch CurrentPalette — Aurora's callback owns it
      // when Aurora is the source.
      if (wispConfig.sourceMode() == wisp::WispSourceMode::Manual) {
        pushManualPaletteToCurrent();
        // Indicator ring reflects the manual palette only when Manual is
        // the active source; in Off/Aurora the persisted palette is
        // background state and shouldn't redraw the ring.
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
      // Push a fresh wispStatus right away so the app sees the new state
      // without waiting for the 30s heartbeat. Resets the heartbeat phase.
      statusBeacon.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedWifiChange:
      // Dispatcher already persisted creds via WispConfig and kicked
      // WifiLink::reconnect() + StageBeacon::refreshAdvert(). All that's
      // left on this side is the wispStatus broadcast so the app sees
      // the wifiConnected transition (driven off WiFi.isConnected()
      // inside StatusBeacon) without waiting up to 30s for the heartbeat.
      Serial.println("[wisp] wifi creds updated; STA reconnect + advert refresh kicked");
      statusBeacon.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedOffColor:
      // Off-mode wisp-ring color stored. Only repaint the ring if we're
      // actually in Off mode right now; otherwise the new color is
      // background state that takes effect on the next Off transition.
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
      // Nothing to do; dispatcher already logged what mattered.
      break;
  }
}

// HELLO + CONTROL_OP recv handler. Fires on the WiFi task — keep
// it tight; only protocol parse + bounded memcpy. No logging, no Preferences,
// no ArduinoJson.
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
    // Peer wisp's claim broadcast (also gossip-relayed by lamps from
    // wisps we can't directly hear). Stash it into the WispRoster's
    // shared view; the claim computation runs on the loop task.
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
  // (Observed-zones is added separately via onZoneObserved_ — that fires
  //  on every state announcement, not just resolved palettes. Still safe
  //  to also record here in case a resolve outpaces the announce path.)
  zoneSelector.observe(zone);

  // First-seen latch: only when neither NVS nor an app op has chosen a zone.
  if (zoneSelector.latchFirstSeen(zone)) {
    Serial.printf("[wisp] claimed Aurora zone %d (source=firstSeen)\n", zone);
    // First-seen latch is a zone change too — fan it out so the app pane
    // can show "the wisp picked zone N".
    statusBeacon.triggerOnChange();
  }

  if (zone != zoneSelector.currentZone()) {
    Serial.printf("[wisp] ignoring zone %d palette (selected %d, source=%s)\n",
                  zone, zoneSelector.currentZone(),
                  wisp::zoneSourceName(zoneSelector.source()));
    return;
  }

  // Source-mode gate: only the Aurora mode lets Aurora callbacks drive
  // CurrentPalette. Manual / Off keep whatever palette the source-mode
  // transition installed (or none, for Off). Skipping here also keeps
  // PaintDistributor::onPaletteChanged() quiet so the operator's manual
  // palette doesn't get bulldozed on the next Aurora notify.
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
  // A live Aurora palette arrived — ensure paint is on (the mode transition
  // leaves it off when the stream wasn't up yet) and fan the new colors out.
  paintDistributor.setPaintMode(true);
  artnetEmitter.onPaletteChanged();
  // Reflect the new Aurora palette on the indicator ring.
  renderRing();
}

// Serial command handler (bench / debug). Parses one stripped line at a time.
// Returns nothing — anything unknown logs back, anything known logs ack.
void processSerialCommand(const String& cmd) {
  if (cmd.length() == 0) return;
  if (cmd == "paint:on") {
    paintDistributor.setPaintMode(true);
    Serial.println("[wisp.cmd] paint mode ON");
  } else if (cmd == "paint:off") {
    paintDistributor.setPaintMode(false);
    Serial.println("[wisp.cmd] paint mode OFF");
  } else if (cmd == "src:off" || cmd == "src:manual" || cmd == "src:aurora") {
    // Bench affordance: source mode is otherwise only settable via the app's
    // setSourceMode wispOp. Route through the same WispConfig + transition the
    // dispatcher uses so serial and BLE-op paths converge.
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
    // Refresh re-reads creds from WispConfig and starts advertising. If
    // creds are empty, it stops — same shape as before but without the
    // explicit ssid/password args.
    stageBeacon.refreshAdvert();
  } else if (cmd == "stage:off") {
    stageBeacon.stop();
  } else if (cmd == "wifi:show") {
    Serial.printf("[wifi] ssid='%s' connected=%d ip=%s\n",
                  wifi.ssid().c_str(), wifi.isConnected() ? 1 : 0,
                  WiFi.localIP().toString().c_str());
  } else if (cmd == "wifi:clear") {
    // Drop stored creds + disconnect + re-pin the radio to
    // LAMP_ESPNOW_CHANNEL. Used for debugging the ESP-NOW channel coex
    // story: once associated to a venue AP, the radio sits on the AP's
    // channel and the wisp's mesh broadcasts miss peers pinned to
    // LAMP_ESPNOW_CHANNEL. WiFi.disconnect alone doesn't reset the
    // channel — we need esp_wifi_set_channel to snap it back.
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
    // Route through the same WispConfig source-of-truth that the setWifi
    // op uses, then kick the two downstream consumers. Mirrors the
    // dispatcher's setWifi chain so serial and BLE-op paths converge.
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

// Drain whatever is in the Serial RX FIFO into serialLineBuf, dispatching
// on newline. Kept inside loop() so we don't need a dedicated task — the
// FreeRTOS timer handles HELLO emission so loop() pacing here is fine.
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
  // Re-sample millis() here rather than trusting the loop()-top nowMs.
  // HELLOs can arrive between the loop-top sample and this dump call
  // (auroraClient.loop() can run for hundreds of ms between them), and
  // lastSeenMs gets stamped with the recv-time millis(). If lastSeenMs
  // is fresher than the captured nowMs, the unsigned subtraction wraps
  // and we'd print age=4294965031ms instead of 18ms.
  const uint32_t nowMs = millis();
  auto roster = inventory.snapshot();
  Serial.printf("[wisp] roster (%u lamp%s):\n",
                (unsigned)roster.size(), roster.size() == 1 ? "" : "s");
  for (const auto& e : roster) {
    // Defensive: if lastSeenMs is still somehow ahead of now (e.g. millis()
    // overflow boundary), clamp to 0 instead of wrapping.
    const uint32_t ageMs = (nowMs >= e.lastSeenMs) ? nowMs - e.lastSeenMs : 0;
    Serial.printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-12s  fw=%s  age=%lums\n",
                  e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
                  e.name.c_str(), formatVersion(e.firmwareVersion).c_str(),
                  (unsigned long)ageMs);
  }
  // Log the ZoneSelector state alongside the roster so the dump is
  // one-stop for "what's this wisp doing?".
  Serial.printf("[wisp] zone=%d source=%s observed=%u\n",
                zoneSelector.currentZone(),
                wisp::zoneSourceName(zoneSelector.source()),
                (unsigned)zoneSelector.observedCount());
}

// Build a stable instance id from the chip MAC's low 24 bits. Aurora uses it
// to recognize a returning subscriber; we want it consistent across reboots.
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

  // Indicator ring: bring the strip up with the global brightness scale
  // applied, then show a warm-white "starting up" state. The first real
  // render() happens after wispConfig.begin() so the cached sourceMode
  // can drive the layout.
  testStrip.begin();
  testStrip.setBrightness(kStatusRingBrightness);
  testStrip.clear();
  testStrip.show();
  Serial.printf("[wisp.ring] %u pixels on GPIO %u (brightness=%u/255)\n",
                (unsigned)wisp::kStatusRingPixelCount,
                (unsigned)kTestStripPin,
                (unsigned)kStatusRingBrightness);

  // Bring NVS up before anything that might want to read selZone. The
  // ZoneSelector seeds itself from the cached value here.
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

  // WifiLink + StageBeacon + ArtnetEmitter — the pre-mesh-compat ArtNet bridge.
  // Both WifiLink and StageBeacon read SSID/password from the shared
  // WispConfig store (single source of truth) and re-read on every
  // reconnect()/refreshAdvert() call. The setWifi op chain
  // (WispOpDispatcher → WifiLink::reconnect + StageBeacon::refreshAdvert)
  // is wired below via wispOpDispatcher.setWifiSinks. StageBeacon and
  // ArtnetEmitter are gated off by default and require an explicit
  // `stage:on` / `artnet:on` serial command (or a setWifi op for the
  // stage advert) to actually start broadcasting.
  wifi.begin(&wispConfig);
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  artnetEmitter.begin(&currentPalette, &wifi);
  // Hand the dispatcher the two sinks it needs to chain a setWifi op into
  // an actual STA reconnect + BLE advert refresh. main.cpp owns the
  // globals; the dispatcher just borrows pointers.
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);

  auroraClient.setInstanceId(buildInstanceId().c_str());
  auroraClient.onActivePalette(onAuroraPalette);
  // Capture every zone we hear about, not just ones whose palettes
  // resolve. This fires on the main task from inside auroraClient.loop().
  auroraClient.onZoneObserved([](int zone) { zoneSelector.observe(zone); });
  auroraClient.begin();
  Serial.printf("[wisp] aurora client started as %s\n",
                buildInstanceId().c_str());

  // Multi-wisp coordination wiring. WispRoster owns the peer-claim
  // shared view + claim-decision logic; PaintDistributor filters its
  // walk by it; StatusBeacon broadcasts our claims every 2 s alongside
  // MSG_WISP_HELLO. Self-MAC is needed for the lower-MAC tiebreaker.
  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);

  // Paint distributor needs the inventory + mesh + palette
  // to walk peers and unicast tuples. Status beacon broadcasts MSG_WISP_HELLO
  // every 2s on a FreeRTOS timer so cadence survives Aurora loop() stalls.
  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);
  paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());

  // Wisp no longer participates in OTA — lamps gossip firmware to each
  // other peer-to-peer. MSG_WISP_HELLO's carriedFw* fields zero-fill
  // (wire layout unchanged for back-compat with older lamps).
  statusBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                     &zoneSelector, &auroraClient, &wispConfig, &wispRoster);
  statusBeacon.startTimer();

  // Apply the persisted source mode now that everything it touches
  // (paintDistributor, currentPalette) is up. This is what makes a
  // Manual-mode wisp start painting from the stored palette on boot
  // without waiting for an app connection.
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
  // Drain any pending MSG_CONTROL_OP payload posted by the recv task. Cheap
  // when empty (one portMUX read + bool check), so safe to call every loop.
  drainPendingWispOp();
  // Multi-wisp coordination: recompute our claim set from the current
  // peer-RSSI view + lamp inventory snapshot before the paint walk so
  // PaintDistributor's filter sees the latest decisions.
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
  // Aurora liveness: if the stream drops while Aurora is the source, fall
  // back to Off (RESTORE peers + clear the buffer) instead of leaving the
  // lamps on the last Aurora palette. Reconnect is handled by onAuroraPalette
  // when a fresh palette arrives. Edge-triggered so we don't re-walk the
  // roster every loop.
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

  // Prune at half the prune window so a dropped lamp leaves the roster
  // within roughly one extra dump cycle. Cheap; just a linear scan.
  if (now - lastPruneMs > 30000) {
    lastPruneMs = now;
    inventory.prune(now, LAMP_PRUNE_TIME_MS);
  }

  delay(5);
}
