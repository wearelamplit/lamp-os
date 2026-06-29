#ifndef LAMP_COMPONENTS_NETWORK_BLUETOOTH_H
#define LAMP_COMPONENTS_NETWORK_BLUETOOTH_H

#include <string>

#include "./bluetooth_pool.hpp"

// Stage manufacturer identifier
#define BLE_STAGE_MAGIC_NUMBER 42007

// Lamp manufacturer identifier
#define BLE_LAMP_MAGIC_NUMBER 42069

// Scan every INTERVAL for WINDOW
#define BLE_GAP_SCAN_INTERVAL_MS 400
#define BLE_GAP_SCAN_WINDOW_MS 15

// Advertise every INTERVAL
#define BLE_GAP_ADV_INTERVAL_MS 1000

// Scan time
#define BLE_GAP_SCAN_TIME_MS 1000

// Advertising intervals
#define BLE_ADVERTISING_INTERVAL_MIN 400
#define BLE_ADVERTISING_INTERVAL_MAX 650

// Tx power level in DB
// @see platformio build flag MYNEWT_VAL_BLE_LL_TX_PWR_DBM as they must match
#define BLE_POWER_LEVEL 4

// Minimum RSSI to be included/updated in the lamp pool
#define BLE_MINIMUM_RSSI_VALUE -94

namespace lamp {

// Stop BLE scan and suppress the automatic restart in onScanEnd.
void bleStopScanNoRestart();

void bleStopAdvertising();

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
  void begin(std::string name, Color inBaseColor, Color inShadeColor);

  /**
   * @brief get a listing of all lamps within acceptable signal strength limits
   * @return vector of all found lamps
   */
  std::vector<BluetoothLampRecord>* getLamps();

  /**
   * @brief get a listing of all stages within acceptable signal strength limits
   * @return vector of all found stages
   */
  std::vector<BluetoothStageRecord>* getStages();
};
}  // namespace lamp
#endif