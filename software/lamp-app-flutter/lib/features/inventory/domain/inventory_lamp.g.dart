// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'inventory_lamp.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_InventoryLamp _$InventoryLampFromJson(Map<String, dynamic> json) =>
    _InventoryLamp(
      id: json['id'] as String,
      name: json['name'] as String,
      controlPassword: json['controlPassword'] as String?,
      critterIndex: (json['critterIndex'] as num?)?.toInt(),
      lastSeenEpochMs: (json['lastSeenEpochMs'] as num?)?.toInt(),
      lastShadeColor: (json['lastShadeColor'] as List<dynamic>?)
          ?.map((e) => (e as num).toInt())
          .toList(),
      lastBaseColor: (json['lastBaseColor'] as List<dynamic>?)
          ?.map((e) => (e as num).toInt())
          .toList(),
      lastKnownIsMesh: json['lastKnownIsMesh'] as bool?,
      lampType: json['lampType'] as String?,
      fwVersion: (json['fwVersion'] as num?)?.toInt(),
      fwChannel: json['fwChannel'] as String?,
      devMode: json['devMode'] as bool? ?? false,
    );

Map<String, dynamic> _$InventoryLampToJson(_InventoryLamp instance) =>
    <String, dynamic>{
      'id': instance.id,
      'name': instance.name,
      'controlPassword': instance.controlPassword,
      'critterIndex': instance.critterIndex,
      'lastSeenEpochMs': instance.lastSeenEpochMs,
      'lastShadeColor': instance.lastShadeColor,
      'lastBaseColor': instance.lastBaseColor,
      'lastKnownIsMesh': instance.lastKnownIsMesh,
      'lampType': instance.lampType,
      'fwVersion': instance.fwVersion,
      'fwChannel': instance.fwChannel,
      'devMode': instance.devMode,
    };
