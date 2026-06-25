// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'wifi_state.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_WifiScanResult _$WifiScanResultFromJson(Map<String, dynamic> json) =>
    _WifiScanResult(
      ssid: json['ssid'] as String,
      rssi: (json['rssi'] as num).toInt(),
      encrypted: json['encrypted'] as bool,
    );

Map<String, dynamic> _$WifiScanResultToJson(_WifiScanResult instance) =>
    <String, dynamic>{
      'ssid': instance.ssid,
      'rssi': instance.rssi,
      'encrypted': instance.encrypted,
    };

_WifiState _$WifiStateFromJson(Map<String, dynamic> json) => _WifiState(
  state: json['state'] as String? ?? 'idle',
  ssid: json['ssid'] as String?,
  ip: json['ip'] as String?,
  lastError: json['lastError'] as String?,
  scanResults:
      (json['scanResults'] as List<dynamic>?)
          ?.map((e) => WifiScanResult.fromJson(e as Map<String, dynamic>))
          .toList() ??
      const <WifiScanResult>[],
);

Map<String, dynamic> _$WifiStateToJson(_WifiState instance) =>
    <String, dynamic>{
      'state': instance.state,
      'ssid': instance.ssid,
      'ip': instance.ip,
      'lastError': instance.lastError,
      'scanResults': instance.scanResults,
    };
