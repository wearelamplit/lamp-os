#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include <cstring>

#include "paint/current_palette.hpp"
#include "fleet/lamp_inventory.hpp"
#include "net/mesh_link.hpp"
#include "net/mesh_router.hpp"
#include "paint/paint_distributor.hpp"
#include "status/presence_beacon.hpp"
#include "status/status_emitter.hpp"
#include "status/seq_source.hpp"
#include "fleet/wisp_roster.hpp"
#include "status/status_ring.hpp"
#include "config/wisp_config.hpp"
#include "config/wisp_op_dispatcher.hpp"
#include "config/zone_selector.hpp"
#include "core/wisp_controller.hpp"
#include "aurora/AuroraPaletteClient.h"

#include "artnet/artnet_emitter.hpp"
#include "net/stage_beacon.hpp"
#include "net/wifi_link.hpp"
#include "console/serial_console.hpp"

namespace {

wisp::MeshLink mesh;
wisp::LampInventory inventory;
wisp::CurrentPalette currentPalette;
wisp::PaintDistributor paintDistributor;
wisp::SeqSource wispSeq;
wisp::StatusEmitter statusEmitter;
wisp::PresenceBeacon presenceBeacon;
wisp::WispRoster wispRoster;
AuroraPaletteClient auroraClient;
wisp::WifiLink wifi;
wisp::StageBeacon stageBeacon;
wisp::ArtnetEmitter artnetEmitter;

// Pre-mesh lamps join this softAP (advertised over the stage beacon) to receive
// ArtNet. Hosted unconditionally on the mesh channel: same-radio, same-channel
// WiFi doesn't disturb ESP-NOW, so serving costs the mesh nothing.
constexpr char kStageApSsid[] = "wisp-stage";
constexpr char kStageApPass[] = "lamplight";

// GPIO 1 (D1): D0 = GPIO 0 = BOOT strap pin; leaving it free keeps
// USB-recover (download mode) working without unplugging the strip.
constexpr uint8_t  kTestStripPin        = 1;
Adafruit_NeoPixel testStrip(wisp::kStatusRingPixelCount, kTestStripPin,
                            NEO_GRB + NEO_KHZ800);
// WS2812 at full power is dazzling at desk distance; 40/255 ≈ 16% reads clearly.
constexpr uint8_t  kStatusRingBrightness = 40;

wisp::WispConfig wispConfig;
wisp::WispOpDispatcher wispOpDispatcher(wispConfig);
wisp::ZoneSelector zoneSelector;

wisp::WispController controller(currentPalette, paintDistributor, wispConfig,
                                zoneSelector, auroraClient, statusEmitter,
                                artnetEmitter, testStrip);

wisp::MeshRouter meshRouter(
    inventory, wispRoster, wispOpDispatcher,
    [](wisp::DispatchResult r) { controller.applyOpResult(r); });

// Stable across reboots; Aurora uses it to recognize returning subscribers.
String buildInstanceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "wisp-%06lx",
           (unsigned long)(mac & 0xFFFFFFul));
  return String(buf);
}

wisp::SerialConsole serialConsole(
    paintDistributor, wispConfig, stageBeacon,
    wifi, statusEmitter, inventory, zoneSelector,
    [](wisp::WispSourceMode m) { controller.applySourceModeTransition(m); });

}  // namespace

void setup() {
  Serial.begin(115200);
  // USB-CDC needs a moment after USB enumerate before printf is ready.
  delay(200);
  Serial.println("wisp: boot");

  // First ring render is deferred until after wispConfig.begin() so the
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

  mesh.onPacket([](const uint8_t* srcMac, const uint8_t* data, size_t len,
                   int8_t rssi) {
    meshRouter.onPacket(srcMac, data, len, rssi);
  });
  if (!mesh.begin()) {
    Serial.println("[wisp] mesh init failed; will retry in 5s");
  }

  wifi.begin(&wispConfig);
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  wifi.startSoftAp(kStageApSsid, kStageApPass);
  stageBeacon.advertiseCreds(kStageApSsid, kStageApPass);
  artnetEmitter.begin(&currentPalette, &wifi);
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);

  auroraClient.setInstanceId(buildInstanceId().c_str());
  auroraClient.onActivePalette(
      [](int zone, const Palette& p) { controller.onAuroraPalette(zone, p); });
  auroraClient.onZoneObserved([](int zone) { zoneSelector.observe(zone); });
  auroraClient.begin();
  Serial.printf("[wisp] aurora client started as %s\n",
                buildInstanceId().c_str());

  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);

  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);
  paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());
  paintDistributor.setDriftInterval(wispConfig.driftIntervalMs(),
                                    wispConfig.driftFadePct());

  // carriedFw* zero-fill; wire layout retained for back-compat with older lamps.
  statusEmitter.begin(&mesh, &zoneSelector, &auroraClient, &wispConfig,
                      &currentPalette, &wispSeq);
  presenceBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                       &auroraClient, &wispRoster, &wispSeq, &statusEmitter);
  statusEmitter.startTimer();
  presenceBeacon.startTimer();

  controller.applySourceModeTransition(wispConfig.sourceMode());

  Serial.printf("[wisp] paint distributor + status beacon online; softAP '%s' up\n",
                kStageApSsid);
  Serial.println("[wisp] cmds: paint:on/off  stage:on/off");
  Serial.println("[wisp] cmds: src:off/manual/aurora  wifi:set <ssid> <pass>  wifi:show");
}

void loop() {
  static uint32_t lastDumpMs = 0;
  static uint32_t lastPruneMs = 0;
  const uint32_t now = millis();

  auroraClient.loop();
  serialConsole.pump();
  meshRouter.drainPendingOps();
  // Beacon work runs here on the 8KB loop stack; the FreeRTOS timers only flag.
  presenceBeacon.pump();
  statusEmitter.pump();
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
  controller.tickAuroraLiveness();

  paintDistributor.tick(now);
  artnetEmitter.tick(now);

  if (now - lastDumpMs > 10000) {
    lastDumpMs = now;
    serialConsole.dumpInventory();
  }

  if (now - lastPruneMs > 30000) {
    lastPruneMs = now;
    inventory.prune(now, LAMP_PRUNE_TIME_MS);
  }

  delay(5);
}
