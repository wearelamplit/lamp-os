import 'package:freezed_annotation/freezed_annotation.dart';

part 'wifi_state.freezed.dart';
part 'wifi_state.g.dart';

/// One row from `wifiState.scanResults`.
@freezed
abstract class WifiScanResult with _$WifiScanResult {
  const factory WifiScanResult({
    required String ssid,
    required int rssi,
    required bool encrypted,
  }) = _WifiScanResult;

  factory WifiScanResult.fromJson(Map<String, dynamic> j) =>
      _$WifiScanResultFromJson(j);
}

/// Decoded snapshot of `BleUuids.wifiState`. The `state` string mirrors
/// the firmware's enum: `idle | scanning | connecting | connected | failed`.
@freezed
abstract class WifiState with _$WifiState {
  const factory WifiState({
    @Default('idle') String state,
    String? ssid,
    String? ip,
    String? lastError,
    @Default(<WifiScanResult>[]) List<WifiScanResult> scanResults,
  }) = _WifiState;

  factory WifiState.fromJson(Map<String, dynamic> j) =>
      _$WifiStateFromJson(j);
}
