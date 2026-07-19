// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'lamp_nearby_peer.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_LampNearbyPeer _$LampNearbyPeerFromJson(Map<String, dynamic> json) =>
    _LampNearbyPeer(
      name: json['name'] as String,
      lampId: json['lampId'] as String? ?? '',
      rssi: (json['rssi'] as num?)?.toInt() ?? -127,
      baseRgbw: json['base'] == null
          ? const <int>[0, 0, 0, 0]
          : _rgbwFromJson(json['base']),
      shadeRgbw: json['shade'] == null
          ? const <int>[0, 0, 0, 0]
          : _rgbwFromJson(json['shade']),
      viaBle: json['viaBle'] as bool? ?? false,
      viaEspNow: json['viaEspNow'] as bool? ?? false,
      lastSeenMs: (json['lastSeenMs'] as num?)?.toInt() ?? 0,
      fwVersion: (json['fwVersion'] as num?)?.toInt() ?? 0,
      otaState: (json['otaState'] as num?)?.toInt() ?? 0,
    );

Map<String, dynamic> _$LampNearbyPeerToJson(_LampNearbyPeer instance) =>
    <String, dynamic>{
      'name': instance.name,
      'lampId': instance.lampId,
      'rssi': instance.rssi,
      'base': instance.baseRgbw,
      'shade': instance.shadeRgbw,
      'viaBle': instance.viaBle,
      'viaEspNow': instance.viaEspNow,
      'lastSeenMs': instance.lastSeenMs,
      'fwVersion': instance.fwVersion,
      'otaState': instance.otaState,
    };
