// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'lamp_nearby_peer.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_LampNearbyPeer _$LampNearbyPeerFromJson(Map<String, dynamic> json) =>
    _LampNearbyPeer(
      name: json['name'] as String,
      bdAddr: json['bdAddr'] as String? ?? '',
      rssi: (json['rssi'] as num?)?.toInt() ?? -127,
      proximity: (json['proximity'] as num?)?.toInt() ?? 2,
      baseRgbw:
          (json['base'] as List<dynamic>?)
              ?.map((e) => (e as num).toInt())
              .toList() ??
          const <int>[0, 0, 0, 0],
      shadeRgbw:
          (json['shade'] as List<dynamic>?)
              ?.map((e) => (e as num).toInt())
              .toList() ??
          const <int>[0, 0, 0, 0],
      viaBle: json['viaBle'] as bool? ?? false,
      viaEspNow: json['viaEspNow'] as bool? ?? false,
      lastSeenMs: (json['lastSeenMs'] as num?)?.toInt() ?? 0,
    );

Map<String, dynamic> _$LampNearbyPeerToJson(_LampNearbyPeer instance) =>
    <String, dynamic>{
      'name': instance.name,
      'bdAddr': instance.bdAddr,
      'rssi': instance.rssi,
      'proximity': instance.proximity,
      'base': instance.baseRgbw,
      'shade': instance.shadeRgbw,
      'viaBle': instance.viaBle,
      'viaEspNow': instance.viaEspNow,
      'lastSeenMs': instance.lastSeenMs,
    };
