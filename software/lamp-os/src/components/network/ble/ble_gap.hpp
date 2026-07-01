#pragma once

// BLE GAP / advertising tunables, shared by the BLE component (bluetooth.cpp)
// and the control service (ble_control.cpp) so the control service doesn't
// pull in the whole BluetoothComponent header just for a scan constant.

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
