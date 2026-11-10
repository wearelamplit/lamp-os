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
#include "core/wisp_controller.hpp"

#include "artnet/artnet_emitter.hpp"
#include "net/stage_beacon.hpp"
#include "net/eth_link.hpp"
#include "net/sta_join.hpp"
#include "net/wifi_link.hpp"
#include "console/serial_console.hpp"
#include "osc/osc_listener.hpp"

namespace {

wisp::MeshLink mesh;
wisp::LampInventory inventory;
wisp::CurrentPalette currentPalette;
wisp::PaintDistributor paintDistributor;
wisp::SeqSource wispSeq;
wisp::StatusEmitter statusEmitter;
wisp::PresenceBeacon presenceBeacon;
wisp::WispRoster wispRoster;
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

// GPIO 1 (D1). The C6's BOOT strap is GPIO 9, which is not on the header, so
// download mode works whatever the strip is wired to.
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

wisp::WispController controller(currentPalette, paintDistributor, wispConfig,
                                statusEmitter, artnetEmitter, testStrip);

wisp::MeshRouter meshRouter(
    inventory, wispRoster, wispOpDispatcher,
    [](wisp::DispatchResult r) { controller.applyOpResult(r); });

// Stable across reboots; the stage beacon's BLE device name.
String buildInstanceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "wisp-%06lx",
           (unsigned long)(mac & 0xFFFFFFul));
  return String(buf);
}

wisp::SerialConsole serialConsole(
    paintDistributor, wispConfig, stageBeacon,
    wifi, statusEmitter, inventory,
    [](wisp::WispSourceMode m) { controller.applySourceModeTransition(m); });

wisp::OscListener oscListener(controller, wispConfig);

}  // namespace

void setup() {
  // A 16-colour palette frame is ~280 B before SLIP escaping, past the 256 B
  // default CDC RX buffer; 2 KB holds a 50-colour frame, escapes included,
  // across a slow loop pass.
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  serialConsole.setOscSink(&oscListener);
  // USB-CDC needs a moment after USB enumerate before printf is ready.
  delay(200);
  Serial.println("wisp: boot");

  // Ring init is deferred: applyLedConfig() sets format + length from NVS
  // after wispConfig.begin() and then calls begin()+renderRing().
  testStrip.setBrightness(kStatusRingBrightness);

  wispConfig.begin();

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

  // Set self-MAC as soon as the radio is up. Until then a unicast CONTROL_OP
  // addressed to this wisp drops against a zero self-MAC.
  uint8_t selfMac[6] = {0};
  mesh.getMac(selfMac);
  wispRoster.setSelfMac(selfMac);
  meshRouter.setSelfMac(selfMac);

  wifi.begin(&wispConfig);
  wisp::eth::begin(stageApSsid(wispConfig).c_str());
  stageBeacon.begin(buildInstanceId().c_str(), &wispConfig);
  const std::string ssid = stageApSsid(wispConfig);
  wifi.startSoftAp(ssid.c_str(), kStageApPass);
  stageBeacon.advertiseCreds(ssid, kStageApPass);
  artnetEmitter.begin(&currentPalette, &wifi);
  wispOpDispatcher.setWifiSinks(&wifi, &stageBeacon);
  wispOpDispatcher.setNonces(&wispOpNonces);

  paintDistributor.begin(&inventory, &mesh, &currentPalette, &wispRoster);
  paintDistributor.setShuffleSeed(wispConfig.shuffleSeed());
  paintDistributor.setDriftInterval(wispConfig.driftIntervalMs(),
                                    wispConfig.driftFadePct());
  paintDistributor.setBrightness(wispConfig.brightness());

  // Random start seq: after a quick double reboot, restarting at 0 would
  // replay (mac, type, seq) tuples still cached in peers' dedup rings.
  wispSeq.counter = static_cast<uint16_t>(esp_random());

  // carriedFw* zero-fill; wire layout retained for back-compat with older lamps.
  statusEmitter.begin(&mesh, &controller, &wispConfig, &currentPalette,
                      &wispSeq, &wifi);
  wifi.setOnChangeCallback([] { statusEmitter.requestEmit(); });
  presenceBeacon.begin(&mesh, &paintDistributor, &currentPalette,
                       &controller, &wispRoster, &wispSeq, &statusEmitter,
                       &wispConfig);
  statusEmitter.startTimer();
  presenceBeacon.startTimer();

  controller.applySourceModeTransition(wispConfig.sourceMode());

  Serial.println("[wisp] paint distributor + status beacon online");
  Serial.println("[wisp] cmds: paint:on/off  stage:on/off");
  Serial.println("[wisp] cmds: src:off/manual/osc  osc:role <primary|secondary|tertiary|fill>");
  Serial.println("[wisp] cmds: wifi:set <ssid> <pass>  wifi:show  eth:show");
}

void loop() {
  static uint32_t lastDumpMs = 0;
  static uint32_t lastPruneMs = 0;
  static uint32_t lastClaimsMs = 0;
  const uint32_t now = millis();

  wifi.loop(wisp::staWanted(
                wispConfig.sourceMode() == wisp::WispSourceMode::Osc,
                wisp::eth::up()),
            now);
  serialConsole.pump();
  meshRouter.drainPendingOps();
  // Beacon work runs here on the 8KB loop stack; the FreeRTOS timers only flag.
  presenceBeacon.pump(statusEmitter.pump());
  // Claims are consumed on the 2 s broadcast tick; recompute at the same cadence.
  if (now - lastClaimsMs >= 2000) {
    lastClaimsMs = now;
    wisp::LampObservation obs[wisp::WISP_ROSTER_MAX_LAMPS];
    const size_t n = inventory.copyObservations(obs, wisp::WISP_ROSTER_MAX_LAMPS);
    wispRoster.recomputeClaims(obs, n, now);
    paintDistributor.refreshDriftRoster(/*paintNewcomers=*/true);
  }
  oscListener.poll(wispConfig.sourceMode() == wisp::WispSourceMode::Osc, now);
  controller.tickOscLiveness(millis());
  controller.tickRingFade(now);

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
