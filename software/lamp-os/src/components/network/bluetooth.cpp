/**
 *  Lamp Bluetooth Management. Pure-BLE v1 — no WiFi, no stage mode, no ArtNet.
 */
#include "bluetooth.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>

// arduino-esp32 3.3.9 defines `btInUse()` as a weak alias for an internal
// `_btInUse_default` that returns `true`. The framework's startup code in
// esp32-hal-misc.c uses btInUse() to decide whether to call
// esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) BEFORE setup() runs.
// On 3.3.0 the strong override pulled into the link; on 3.3.9 only the
// weak alias is visible at link time, the framework picks the weak default
// (return false), releases the BT controller's BTDM memory, and the
// subsequent NimBLEDevice::init()/esp_bt_controller_init() returns
// ESP_ERR_INVALID_STATE (259). Defining a STRONG btInUse() here forces
// the framework to skip the mem-release call.
extern "C" bool btInUse() { return true; }

#include <string>
#include <vector>

#include "config/config.hpp"
#include "util/color.hpp"
#include "ble_control.hpp"
#include "nearby_lamps.hpp"

namespace lamp {

// Cached manufacturer-data vector. `begin()` populates it;
// `setAdvertisedColors()` rebuilds + re-applies when shade or base
// changes. Lives at file scope so we can compare-and-skip when the
// new buffer matches the last applied one.
static std::vector<unsigned char> s_advertisementData;
// Cached name so the advertisement-rebuild helpers don't need it
// passed in. (NimBLE's setManufacturerData *appends* rather than
// replaces; we rebuild the entire AdvertisementData each time.)
static std::string s_advertisementName;

// Capability bitfield advertised as the trailing manufacturer-data byte.
// Each bit is an independent feature flag the app reads via
// `(byte & WANTED_BIT) != 0`. Forward-compatible: new bits added when
// a real consumer ships; old apps just don't read them.
//
//   bit 0 (0x01) — reserved (always 0; bookmark for legacy v1 lamps
//                  that didn't advertise this byte at all)
//   bit 1 (0x02) — kBleCapMeshProtocol — speaks the v0x03 mesh wire
//                  format. Drives the app's ControlScreen vs
//                  BtOnlyLampScreen routing.
//
// The byte is 0x02 today — bytewise identical to the prior "firmware
// version 0x02" sentinel. v2 apps checking `mfg[6] >= 2` still pass;
// v3+ apps using `(mfg[6] & kBleCapMeshProtocol) != 0` get the same
// answer.
static constexpr unsigned char kBleCapMeshProtocol = 0x02;
static constexpr unsigned char ADV_CAPABILITY_BYTE = kBleCapMeshProtocol;

static void applyAdvertisementPayload(NimBLEAdvertising* adv,
                                      const std::string& name,
                                      const std::vector<unsigned char>& mfg) {
  // Build the advertisement payload from scratch and atomically replace
  // it via `setAdvertisementData`. The high-level setManufacturerData /
  // setName helpers on NimBLEAdvertising both `addData` to an internal
  // accumulator (NimBLEAdvertising.cpp ~ line 270) — a second call
  // doesn't replace the previous mfg field, it appends a SECOND mfg
  // field after it, which trips the 31-byte limit and falls back to the
  // scan-response packet (firing the "Data length exceeded" warning).
  NimBLEAdvertisementData data;
  data.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  data.setName(name, /*isComplete=*/true);
  data.setManufacturerData(mfg);
  adv->setAdvertisementData(data);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  bool isLamp(std::string data) {
    // Accepted mfg payload shapes (all share the same magic16
    // prefix in bytes 0-1):
    //   - 8 bytes [magic, baseRGB, shadeRGB]: v1 firmware (legacy,
    //     pre-mesh). Reads base and shade from adv.
    //   - 6 bytes [magic, baseRGB, meshFlag]: older v2 firmware
    //     during the dropped-shade window. No shade in adv; reads
    //     base only. Accepted for forward compat until every lamp
    //     in the field is re-flashed to the 9-byte shape.
    //   - 9 bytes [magic, baseRGB, shadeRGB, version]: current
    //     firmware (this build). Reads base + shade; `version`
    //     byte at index 8 identifies the build.
    const auto n = data.length();
    return ((n == 6 || n == 8 || n == 9) &&
            data[0] == (BLE_LAMP_MAGIC_NUMBER & 0xff) &&
            data[1] == ((BLE_LAMP_MAGIC_NUMBER >> 8) & 0xff));
  };

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (!advertisedDevice->haveName() || !advertisedDevice->haveManufacturerData()) return;
    if (advertisedDevice->getRSSI() <= BLE_MINIMUM_RSSI_VALUE) return;
    std::string data = advertisedDevice->getManufacturerData();
    if (!isLamp(data)) return;
    // ESP32 controller will sometimes surface our own adv to our own scan
    // when adv + scan overlap. Without this filter the lamp would appear in
    // its own NearbyLamps list and surface in the app's Social tab as a
    // "seen" peer. Match by BLE address (the lamp's name is user-set and
    // can collide; the address is unique). ESP-NOW already does the
    // equivalent filter in show_receiver.cpp:175 via sourceMac vs myMac_.
    if (advertisedDevice->getAddress() == NimBLEDevice::getAddress()) return;

    Color base(data[2], data[3], data[4], 0);
    // Shade is at bytes 5-7 for both v1 (8-byte) and current v2 (9-byte).
    // For the legacy 6-byte v2 shape (no shade in adv), default to black.
    const bool hasShade = (data.length() == 8 || data.length() == 9);
    Color shade = hasShade
                      ? Color(data[5], data[6], data[7], 0)
                      : Color(0, 0, 0, 0);

    // Canonical uppercase colon-hex BD_ADDR for the disposition
    // cross-reference. NimBLEAddress::toString() returns lowercase; we
    // uppercase to match the format used elsewhere in this codebase
    // (ble_control.cpp:606, nearby_lamps.cpp:446).
    std::string bdAddr = advertisedDevice->getAddress().toString();
    for (char& c : bdAddr) {
      if (c >= 'a' && c <= 'f') c = c - 'a' + 'A';
    }

    nearbyLamps.addOrUpdateFromBle(advertisedDevice->getName(), bdAddr,
                                   base, shade,
                                   static_cast<int8_t>(advertisedDevice->getRSSI()));
  };

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    nearbyLamps.prune(LAMP_PRUNE_TIME_MS);
    // Skip restart while a phone is using the GATT control service.
    // ble_control resumes the scan on disconnect.
    if (!ble_control::isScanPaused()) {
      NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);
    }
  }
} scanCallbacks;

BluetoothComponent::BluetoothComponent() {};

void BluetoothComponent::begin(std::string name, Color inBaseColor,
                               Color inShadeColor) {
#ifdef LAMP_DEBUG
  Serial.printf("Starting Bluetooth Async Client\n");
#endif
  NimBLEDevice::init(name.substr(0, 12));
  NimBLEDevice::setPower(BLE_POWER_LEVEL);

  // LE Secure Connections + Just-Works bonding remain enabled, but the
  // link layer no longer forces encryption on any characteristic — see
  // `app-layer crypto` below. Sensitive writes (CHAR_AUTH, CHAR_WIFI_OP,
  // CHAR_REMOTE_OP, CHAR_SETTINGS_BLOB) accept an app-layer AES-GCM
  // frame keyed off the lamp password via `lamp::crypto`; legacy
  // plaintext writes still work for the webapp/old clients. The OS
  // will not pop a pair dialog on any write. Phones bonded under the
  // old WRITE_ENC scheme still re-encrypt silently because their bond
  // record is still valid; fresh phones simply skip the bond altogether.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true,
                                /*mitm=*/false,
                                /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(BLE_GAP_ADV_INTERVAL_MS);
  pScan->setWindow(BLE_GAP_SCAN_WINDOW_MS);
  pScan->setActiveScan(true);
  pScan->start(BLE_GAP_SCAN_TIME_MS);

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  s_advertisementName = name;
  // Scan response stays off — we build the entire payload into the main
  // advertisement packet via setAdvertisementData below, which gives us
  // deterministic control over what goes on the wire.
  pAdvertising->enableScanResponse(false);
  // Adv payload shape: [magic16(2), baseRGB(3), shadeRGB(3), capabilities(1)]
  // = 9 bytes total. byte 8 was previously a "firmware version" byte hard-
  // coded to 0x02; it's now a forward-compatible capability bitfield
  // (see ADV_CAPABILITY_BYTE comment above). The byte's value is still
  // 0x02 today (only bit 1 "mesh protocol" is set), so v2 apps continue
  // to route correctly. Legacy v1 8-byte advs (no capability byte) and
  // intermediate v2 6-byte advs (no shade) are still tolerated by the
  // scanner above for cross-firmware compat.
  s_advertisementData = {
      static_cast<unsigned char>(BLE_LAMP_MAGIC_NUMBER & 0xff),
      static_cast<unsigned char>((BLE_LAMP_MAGIC_NUMBER >> 8) & 0xff),
      static_cast<unsigned char>(inBaseColor.r),
      static_cast<unsigned char>(inBaseColor.g),
      static_cast<unsigned char>(inBaseColor.b),
      static_cast<unsigned char>(inShadeColor.r),
      static_cast<unsigned char>(inShadeColor.g),
      static_cast<unsigned char>(inShadeColor.b),
      ADV_CAPABILITY_BYTE,
  };
  applyAdvertisementPayload(pAdvertising, s_advertisementName,
                            s_advertisementData);
  pAdvertising->setConnectableMode(BLE_GAP_CONN_MODE_UND);
  pAdvertising->setMinInterval(BLE_ADVERTISING_INTERVAL_MIN);
  pAdvertising->setMaxInterval(BLE_ADVERTISING_INTERVAL_MAX);
  // Advertising start is deferred to activateGattServices() — NimBLE's
  // GATT database is frozen once advertising starts.
  Serial.printf("[ble] advertising configured for name=%s (deferred start)\n",
                name.c_str());
};

// Minimum gap between NimBLE adv-data updates. Calling
// setAdvertisementData() faster than the advertising interval
// corrupts the host task's pending buffer and panics the lamp
// (see `_invalid_pc_placeholder` repro in commit history).
// 250ms = 2× the slow end of BLE_ADVERTISING_INTERVAL_MAX (96 * 0.625ms
// ≈ 60ms) with plenty of safety margin.
static constexpr uint32_t ADV_FLUSH_MIN_GAP_MS = 250;

void BluetoothComponent::setAdvertisedColors(Color base, Color shade) {
  // Fast setter: just record the latest colors. The actual NimBLE
  // update happens on the next tickAdvertising() that's outside the
  // debounce window. Multiple back-to-back calls collapse to the
  // last-write-wins values.
  m_pendingAdvBase = base;
  m_pendingAdvShade = shade;
  m_advDirty = true;
}

void BluetoothComponent::tickAdvertising() {
  if (!m_advDirty) return;
  const uint32_t now = millis();
  if (now - m_lastAdvFlushMs < ADV_FLUSH_MIN_GAP_MS) return;
  if (s_advertisementData.size() < 9) return;  // begin() hasn't run

  const Color base = m_pendingAdvBase;
  const Color shade = m_pendingAdvShade;
  const unsigned char newBytes[6] = {
      static_cast<unsigned char>(base.r),
      static_cast<unsigned char>(base.g),
      static_cast<unsigned char>(base.b),
      static_cast<unsigned char>(shade.r),
      static_cast<unsigned char>(shade.g),
      static_cast<unsigned char>(shade.b),
  };
  bool changed = false;
  for (int i = 0; i < 6; ++i) {
    if (s_advertisementData[2 + i] != newBytes[i]) {
      s_advertisementData[2 + i] = newBytes[i];
      changed = true;
    }
  }
  // Clear dirty + stamp the flush time regardless of whether the
  // bytes differed. If a re-call set the same colors, we don't want
  // to keep re-evaluating it every tick.
  m_advDirty = false;
  m_lastAdvFlushMs = now;
  if (!changed) return;

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  if (pAdvertising == nullptr) return;
  applyAdvertisementPayload(pAdvertising, s_advertisementName,
                            s_advertisementData);
#ifdef LAMP_DEBUG
  Serial.printf("[ble] adv colors updated base=%02x%02x%02x shade=%02x%02x%02x\n",
                base.r, base.g, base.b, shade.r, shade.g, shade.b);
#endif
}

void BluetoothComponent::activateGattServices(Config* cfg, Preferences* prefs) {
  // NimBLE's ble_gatts_mutable() returns false if any GAP procedure is
  // active, and ble_gatts_add_svcs() silently drops services if so. Pause
  // the central scan while registering services + starting advertising.
  NimBLEScan* scan = NimBLEDevice::getScan();
  bool wasScanning = scan->isScanning();
  if (wasScanning) {
    scan->stop();
    Serial.printf("[ble] stopped central scan for GATT registration\n");
  }

  // ble_setup service deleted: provisioning now rides settings_blob on
  // ble_control. The control service's `isAuthed` returns true while
  // `lamp.password` is empty (factory default), so a fresh lamp accepts
  // the initial claim write unauthenticated; after the password is set
  // the GCM handshake is required for every subsequent write. One
  // service, one auth model.
  ble_control::start(cfg, prefs);

  NimBLEDevice::getServer()->start();
  Serial.printf("[ble] server.start() done (GATT services registered)\n");

  bool advStarted = NimBLEDevice::getAdvertising()->start();
  Serial.printf("[ble] advertising started=%d\n", advStarted);

  if (wasScanning) {
    scan->start(0);  // 0 = continuous
    Serial.printf("[ble] central scan restarted\n");
  }
}

}  // namespace lamp
