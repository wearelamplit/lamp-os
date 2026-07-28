#pragma once

// BLE GAP / advertising tunables, shared by the BLE component (bluetooth.cpp)
// and the control service (ble_control.cpp) so the control service doesn't
// pull in the whole BluetoothComponent header just for a scan constant.

// Lamp manufacturer identifier
#define BLE_LAMP_MAGIC_NUMBER 42069

// BLE scan window. pScan->setWindow(BLE_GAP_SCAN_WINDOW_MS) in
// bluetooth.cpp. Kept short under SW-coex to leave airtime for ESP-NOW.
#define BLE_GAP_SCAN_WINDOW_MS 15

// Central-scan interval. setInterval(BLE_GAP_ADV_INTERVAL_MS) with
// BLE_GAP_SCAN_WINDOW_MS gives the continuous ~1.5% scan duty.
#define BLE_GAP_ADV_INTERVAL_MS 1000

// Advertising intervals (BLE units of 0.625 ms): 160 = 100 ms, 320 = 200 ms.
// Max stays under ADV_FLUSH_MIN_GAP_MS (250 ms) so a color-update flush never
// outruns an adv event. bluetooth.cpp adds a MAC-seeded per-lamp offset within
// this band to break boot-lockstep across a fleet powering on together.
#define BLE_ADVERTISING_INTERVAL_MIN 160
#define BLE_ADVERTISING_INTERVAL_MAX 320

// Tx power level in DB
// @see platformio build flag MYNEWT_VAL_BLE_LL_TX_PWR_DBM as they must match
#define BLE_POWER_LEVEL 4

// Minimum RSSI to be included/updated in the lamp pool
#define BLE_MINIMUM_RSSI_VALUE -94
