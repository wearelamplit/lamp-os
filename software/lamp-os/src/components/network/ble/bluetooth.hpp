#pragma once

#include <string>

#include "config/config.hpp"

// Lamp manufacturer identifier
#define BLE_LAMP_MAGIC_NUMBER 42069

// BLE scan window — pScan->setWindow(BLE_GAP_SCAN_WINDOW_MS) in
// bluetooth.cpp. Kept short under SW-coex to leave airtime for ESP-NOW.
#define BLE_GAP_SCAN_WINDOW_MS 15

// Advertise every INTERVAL
#define BLE_GAP_ADV_INTERVAL_MS 1000

// Scan time
#define BLE_GAP_SCAN_TIME_MS 1000

// Advertising intervals (BLE units of 0.625 ms). Lamp is mains-powered so
// no reason not to advertise fast.
#define BLE_ADVERTISING_INTERVAL_MIN 48
#define BLE_ADVERTISING_INTERVAL_MAX 96

// Tx power level in DB
// @see platformio build flag MYNEWT_VAL_BLE_LL_TX_PWR_DBM as they must match
#define BLE_POWER_LEVEL 4

// Minimum RSSI to be included/updated in the lamp pool
#define BLE_MINIMUM_RSSI_VALUE -94

namespace lamp {
/**
 * @brief Entrypoint class to advertise and track lamps by Bluetooth LE
 */
class BluetoothComponent {
 public:
  BluetoothComponent();

  /**
   * @brief initialize bluetooth with the user's lamp name and colors
   * @param [in] name max. 13 character string representing the lamp's name
   * @param [in] inBaseColor the base color RGB value. W is ommitted
   * @param [in] inShadeColor the shade color RGB value. W is ommitted
   */
  void begin(std::string name, Color inBaseColor, Color inShadeColor,
             bool configured);

  /**
   * @brief register the GATT setup + control services, start the GATT server,
   * start advertising. Must be called after begin() and after any preferences
   * loaded. The scan-stop/restart bracket is handled internally because
   * NimBLE's ble_gatts_mutable() returns false if any GAP procedure is
   * active when ble_gatts_add_svcs runs (silent service registration drop).
   */
  void activateGattServices(Config* cfg);

  /**
   * @brief record the latest base + shade colors that should be
   * reflected in the BLE advertisement. This is a *fast setter* —
   * it does not touch NimBLE. The actual NimBLE update happens
   * in tickAdvertising() on a debounced schedule so rapid
   * baseColors writes (e.g. a user dragging the color picker)
   * cannot starve the BLE host task. Mfg payload shape is
   * `[magic16, baseRGB, shadeRGB, version=0x02]` = 9 bytes.
   *
   * Synchronously calling NimBLE's setAdvertisementData() from
   * the loop task at sub-100ms intervals corrupts the host
   * task's pending-advertisement buffer and crashes the lamp
   * with `_invalid_pc_placeholder`. Don't do that again.
   */
  void setAdvertisedColors(Color base, Color shade);

  /**
   * @brief flush any pending advertisement-color update to
   * NimBLE if at least the debounce interval has elapsed since
   * the last flush. Call once per main-loop tick.
   */
  void tickAdvertising();

 private:
  Color m_pendingAdvBase;
  Color m_pendingAdvShade;
  bool m_advDirty = false;
  uint32_t m_lastAdvFlushMs = 0;
};
}  // namespace lamp
