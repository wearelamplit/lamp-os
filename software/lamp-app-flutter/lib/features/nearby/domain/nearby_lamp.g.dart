// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'nearby_lamp.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_NearbyLamp _$NearbyLampFromJson(Map<String, dynamic> json) => _NearbyLamp(
  id: json['id'] as String,
  name: json['name'] as String,
  rssi: (json['rssi'] as num).toInt(),
  serviceUuids: (json['serviceUuids'] as List<dynamic>)
      .map((e) => e as String)
      .toList(),
  baseRgb: (json['baseRgb'] as num).toInt(),
  shadeRgb: (json['shadeRgb'] as num).toInt(),
  lastSeenEpochMs: (json['lastSeenEpochMs'] as num).toInt(),
  isMesh: json['isMesh'] as bool? ?? false,
);

Map<String, dynamic> _$NearbyLampToJson(_NearbyLamp instance) =>
    <String, dynamic>{
      'id': instance.id,
      'name': instance.name,
      'rssi': instance.rssi,
      'serviceUuids': instance.serviceUuids,
      'baseRgb': instance.baseRgb,
      'shadeRgb': instance.shadeRgb,
      'lastSeenEpochMs': instance.lastSeenEpochMs,
      'isMesh': instance.isMesh,
    };
