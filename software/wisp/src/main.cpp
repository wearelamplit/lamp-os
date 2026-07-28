#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <esp_random.h>

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
// ArtNet. Hosted unconditionally on the mesh channel: the single radio stays
// pinned there, so same-channel WiFi doesn't disturb ESP-NOW. SSID is
// wisp-<name>, falling back when unnamed, to be recognizable in a WiFi scan.
constexpr char kStageApSsidFallback[] = "wisp";
constexpr char kStageApPass[] = "lamplight";

std::string stageApSsid(const wisp::WispConfig& config) {
  const String& name = config.name();
  if (name.length() == 0) return kStageApSsidFallback;
  return std::string("wisp-") + name.c_str();
}

// GPIO 1 (D1): D0 = GPIO 0 = BOOT strap pin; leaving it free keeps
// USB-recover (download mode) working without unplugging the strip.
constexpr uint8_t  kTestStripPin        = 1;
// Sized to kMaxRingPixels so the internal buffer covers any configured count;
// applyLedConfig() sets the real format + length after config is loaded.
Adafruit_NeoPixel testStrip(wisp::kMaxRingPixels, kTestStripPin,
                            NEO_GRB + NEO_KHZ800);
// WS2812 at full power is dazzling at desk distance; 40/255 ≈ 16% reads clearly.
constexpr uint8_t  kStatusRingBrightness = 40;

wisp::WispConfig wispConfig;
wisp::WispOpDispatcher wispOpDispatcher(wispConfig);
wisp::crypto::RecentNonces wispOpNonces;
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

  // Ring init is deferred: applyLedConfig() sets format + length from NVS
  // after wispConfig.begin() and then calls begin()+renderRing().
  testStrip.setBrightness(kStatusRingBrightness);

  wispConfig.begin();
  if (wispConfig.hasSelectedZone()) {
    const int z = wispConfig.selectedZone();
    zoneSelector.setFromNvs(z);
    Serial.printf("[wisp] zone %d from NVS\n", z);
  } else {
    Serial.println("[wisp] no zone in NVS; will latch first-seen Aurora zone");
  }

  // Apply NVS-configured format + pixel count to the physical strip.
  controller.applyLedConfig();
  Serial.printf("[wisp.ring] %u px on GPIO %u (brightness=%u/255)\n",
                (unsigned)wispConfig.pixelCount(),
                (unsigned)kTestStripPin,
                (unsigned)kStatusRingBrightness);

  mesh.onPacket([](const uint8_t* srcMac, const uint8_t* data, size_t len,
                   int8_t rssi) {
    meshRouter.onPacket(srcMac, data, len, rssi);
  });
  if (!mesh.begin()) {
    Serial.println("[wisp] mesh init failed; will retry in 5s");
  }

  // Set self-MAC before any packet can reach onPacket, else a real unicast
  // CONTROL_OP addressed to us drops against a zero self-MAC.
  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);
  meshRouter.setSelfMac(selfMac);

  wifi.begin(&wispConfig);
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  const std::string ssid = stageApSsid(wispConfig);
  wifi.startSoftAp(ssid.c_str(), kStageApPass);
  stageBeacon.advertiseCreds(ssid, kStageApPass);
  artnetEmitter.begin(&currentPalette, &wifi);
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);
  wispOpDispatcher.setNonces(&wispOpNonces);

  auroraClient.setInstanceId(buildInstanceId().c_str());
  auroraClient.onActivePalette(
      [](int zone, const Palette& p) { controller.onAuroraPalette(zone, p); });
  auroraClient.onZoneObserved([](int zone) { zoneSelector.observe(zone); });
  auroraClient.begin();
  Serial.printf("[wisp] aurora client started as %s\n",
                buildInstanceId().c_str());

  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);
  paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());
  paintDistributor.setDriftInterval(wispConfig.driftIntervalMs(),
                                    wispConfig.driftFadePct());
  paintDistributor.setBrightness(wispConfig.brightness());

  // Random start seq: after a quick double reboot, restarting at 0 would
  // replay (mac, type, seq) tuples still cached in peers' dedup rings.
  wispSeq.counter = static_cast<uint16_t>(esp_random());

  // carriedFw* zero-fill; wire layout retained for back-compat with older lamps.
  statusEmitter.begin(&mesh, &zoneSelector, &auroraClient, &wispConfig,
                      &currentPalette, &wispSeq);
  presenceBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                       &auroraClient, &wispRoster, &wispSeq, &statusEmitter,
                       &wispConfig);
  statusEmitter.startTimer();
  presenceBeacon.startTimer();

  controller.applySourceModeTransition(wispConfig.sourceMode());

  Serial.println("[wisp] paint distributor + status beacon online");
  Serial.println("[wisp] cmds: paint:on/off  stage:on/off");
  Serial.println("[wisp] cmds: src:off/manual/aurora  wifi:set <ssid> <pass>  wifi:show");
}

void loop() {
  static uint32_t lastDumpMs = 0;
  static uint32_t lastPruneMs = 0;
  static uint32_t lastClaimsMs = 0;
  const uint32_t now = millis();

  auroraClient.loop();
  serialConsole.pump();
  meshRouter.drainPendingOps();
  // Beacon work runs here on the 8KB loop stack; the FreeRTOS timers only flag.
  presenceBeacon.pump();
  statusEmitter.pump();
  // Claims are consumed on the 2 s broadcast tick; recompute at the same cadence.
  if (now - lastClaimsMs >= 2000) {
    lastClaimsMs = now;
    wisp::LampObservation obs[wisp::WISP_ROSTER_MAX_LAMPS];
    const size_t n = inventory.copyObservations(obs, wisp::WISP_ROSTER_MAX_LAMPS);
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

#ifdef LAMP_DEBUG
  const uint32_t loopMs = millis() - now;
  if (loopMs > 100) {
    Serial.printf("[wisploop] stall=%ums\n", (unsigned)loopMs);
  }
#endif

  delay(5);
}
