// GENERATED CODE - DO NOT MODIFY BY HAND
// coverage:ignore-file
// ignore_for_file: type=lint
// ignore_for_file: unused_element, deprecated_member_use, deprecated_member_use_from_same_package, use_function_type_syntax_for_parameters, unnecessary_const, avoid_init_to_null, invalid_override_different_default_values_named, prefer_expression_function_bodies, annotate_overrides, invalid_annotation_target, unnecessary_question_mark

part of 'lamp_nearby_peer.dart';

// **************************************************************************
// FreezedGenerator
// **************************************************************************

// dart format off
T _$identity<T>(T value) => value;

/// @nodoc
mixin _$LampNearbyPeer {

 String get name;/// Raw mesh MAC, uppercase colon-hex. Empty if the lamp firmware
/// predates the lampId emit.
 String get lampId;/// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
/// `-127` means "no reading yet" (older firmware that doesn't emit
/// RSSI, or a fresh peer not yet seen via BLE).
 int get rssi;/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
@JsonKey(name: 'base', fromJson: _rgbwFromJson) List<int> get baseRgbw;@JsonKey(name: 'shade', fromJson: _rgbwFromJson) List<int> get shadeRgbw; bool get viaBle; bool get viaEspNow; int get lastSeenMs;/// Packed semver (major<<16 | minor<<8 | patch). 0 = unknown/legacy
/// peer (the lamp omits it when zero).
 int get fwVersion;/// `{type}-{channel}` slot, e.g. "standard-beta". Empty on legacy
/// firmware that predates the emit.
 String get fwChannel;/// OTA state: 0=idle, 1=sending, 2=receiving. Omitted by the lamp
/// when idle.
 int get otaState;/// Mesh MAC (uppercase colon-hex) of the peer this lamp is OTA-sending
/// to. Null unless the lamp is Sending. Resolve against inventory by
/// `lampId` to name the receiver, which is HELLO-silent during its OTA.
 String? get otaSendingTo;/// Firmware-computed proximity: true = near, false = far. The lamp
/// derives this from its own RSSI tiers; the app just renders it.
 bool get near;
/// Create a copy of LampNearbyPeer
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$LampNearbyPeerCopyWith<LampNearbyPeer> get copyWith => _$LampNearbyPeerCopyWithImpl<LampNearbyPeer>(this as LampNearbyPeer, _$identity);

  /// Serializes this LampNearbyPeer to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is LampNearbyPeer&&(identical(other.name, name) || other.name == name)&&(identical(other.lampId, lampId) || other.lampId == lampId)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&const DeepCollectionEquality().equals(other.baseRgbw, baseRgbw)&&const DeepCollectionEquality().equals(other.shadeRgbw, shadeRgbw)&&(identical(other.viaBle, viaBle) || other.viaBle == viaBle)&&(identical(other.viaEspNow, viaEspNow) || other.viaEspNow == viaEspNow)&&(identical(other.lastSeenMs, lastSeenMs) || other.lastSeenMs == lastSeenMs)&&(identical(other.fwVersion, fwVersion) || other.fwVersion == fwVersion)&&(identical(other.fwChannel, fwChannel) || other.fwChannel == fwChannel)&&(identical(other.otaState, otaState) || other.otaState == otaState)&&(identical(other.otaSendingTo, otaSendingTo) || other.otaSendingTo == otaSendingTo)&&(identical(other.near, near) || other.near == near));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,name,lampId,rssi,const DeepCollectionEquality().hash(baseRgbw),const DeepCollectionEquality().hash(shadeRgbw),viaBle,viaEspNow,lastSeenMs,fwVersion,fwChannel,otaState,otaSendingTo,near);

@override
String toString() {
  return 'LampNearbyPeer(name: $name, lampId: $lampId, rssi: $rssi, baseRgbw: $baseRgbw, shadeRgbw: $shadeRgbw, viaBle: $viaBle, viaEspNow: $viaEspNow, lastSeenMs: $lastSeenMs, fwVersion: $fwVersion, fwChannel: $fwChannel, otaState: $otaState, otaSendingTo: $otaSendingTo, near: $near)';
}


}

/// @nodoc
abstract mixin class $LampNearbyPeerCopyWith<$Res>  {
  factory $LampNearbyPeerCopyWith(LampNearbyPeer value, $Res Function(LampNearbyPeer) _then) = _$LampNearbyPeerCopyWithImpl;
@useResult
$Res call({
 String name, String lampId, int rssi,@JsonKey(name: 'base', fromJson: _rgbwFromJson) List<int> baseRgbw,@JsonKey(name: 'shade', fromJson: _rgbwFromJson) List<int> shadeRgbw, bool viaBle, bool viaEspNow, int lastSeenMs, int fwVersion, String fwChannel, int otaState, String? otaSendingTo, bool near
});




}
/// @nodoc
class _$LampNearbyPeerCopyWithImpl<$Res>
    implements $LampNearbyPeerCopyWith<$Res> {
  _$LampNearbyPeerCopyWithImpl(this._self, this._then);

  final LampNearbyPeer _self;
  final $Res Function(LampNearbyPeer) _then;

/// Create a copy of LampNearbyPeer
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? name = null,Object? lampId = null,Object? rssi = null,Object? baseRgbw = null,Object? shadeRgbw = null,Object? viaBle = null,Object? viaEspNow = null,Object? lastSeenMs = null,Object? fwVersion = null,Object? fwChannel = null,Object? otaState = null,Object? otaSendingTo = freezed,Object? near = null,}) {
  return _then(_self.copyWith(
name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,lampId: null == lampId ? _self.lampId : lampId // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,baseRgbw: null == baseRgbw ? _self.baseRgbw : baseRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,shadeRgbw: null == shadeRgbw ? _self.shadeRgbw : shadeRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,viaBle: null == viaBle ? _self.viaBle : viaBle // ignore: cast_nullable_to_non_nullable
as bool,viaEspNow: null == viaEspNow ? _self.viaEspNow : viaEspNow // ignore: cast_nullable_to_non_nullable
as bool,lastSeenMs: null == lastSeenMs ? _self.lastSeenMs : lastSeenMs // ignore: cast_nullable_to_non_nullable
as int,fwVersion: null == fwVersion ? _self.fwVersion : fwVersion // ignore: cast_nullable_to_non_nullable
as int,fwChannel: null == fwChannel ? _self.fwChannel : fwChannel // ignore: cast_nullable_to_non_nullable
as String,otaState: null == otaState ? _self.otaState : otaState // ignore: cast_nullable_to_non_nullable
as int,otaSendingTo: freezed == otaSendingTo ? _self.otaSendingTo : otaSendingTo // ignore: cast_nullable_to_non_nullable
as String?,near: null == near ? _self.near : near // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}

}


/// Adds pattern-matching-related methods to [LampNearbyPeer].
extension LampNearbyPeerPatterns on LampNearbyPeer {
/// A variant of `map` that fallback to returning `orElse`.
///
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case final Subclass value:
///     return ...;
///   case _:
///     return orElse();
/// }
/// ```

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _LampNearbyPeer value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that);case _:
  return orElse();

}
}
/// A `switch`-like method, using callbacks.
///
/// Callbacks receives the raw object, upcasted.
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case final Subclass value:
///     return ...;
///   case final Subclass2 value:
///     return ...;
/// }
/// ```

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _LampNearbyPeer value)  $default,){
final _that = this;
switch (_that) {
case _LampNearbyPeer():
return $default(_that);case _:
  throw StateError('Unexpected subclass');

}
}
/// A variant of `map` that fallback to returning `null`.
///
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case final Subclass value:
///     return ...;
///   case _:
///     return null;
/// }
/// ```

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _LampNearbyPeer value)?  $default,){
final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that);case _:
  return null;

}
}
/// A variant of `when` that fallback to an `orElse` callback.
///
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case Subclass(:final field):
///     return ...;
///   case _:
///     return orElse();
/// }
/// ```

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( String name,  String lampId,  int rssi, @JsonKey(name: 'base', fromJson: _rgbwFromJson)  List<int> baseRgbw, @JsonKey(name: 'shade', fromJson: _rgbwFromJson)  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs,  int fwVersion,  String fwChannel,  int otaState,  String? otaSendingTo,  bool near)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that.name,_that.lampId,_that.rssi,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs,_that.fwVersion,_that.fwChannel,_that.otaState,_that.otaSendingTo,_that.near);case _:
  return orElse();

}
}
/// A `switch`-like method, using callbacks.
///
/// As opposed to `map`, this offers destructuring.
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case Subclass(:final field):
///     return ...;
///   case Subclass2(:final field2):
///     return ...;
/// }
/// ```

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( String name,  String lampId,  int rssi, @JsonKey(name: 'base', fromJson: _rgbwFromJson)  List<int> baseRgbw, @JsonKey(name: 'shade', fromJson: _rgbwFromJson)  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs,  int fwVersion,  String fwChannel,  int otaState,  String? otaSendingTo,  bool near)  $default,) {final _that = this;
switch (_that) {
case _LampNearbyPeer():
return $default(_that.name,_that.lampId,_that.rssi,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs,_that.fwVersion,_that.fwChannel,_that.otaState,_that.otaSendingTo,_that.near);case _:
  throw StateError('Unexpected subclass');

}
}
/// A variant of `when` that fallback to returning `null`
///
/// It is equivalent to doing:
/// ```dart
/// switch (sealedClass) {
///   case Subclass(:final field):
///     return ...;
///   case _:
///     return null;
/// }
/// ```

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( String name,  String lampId,  int rssi, @JsonKey(name: 'base', fromJson: _rgbwFromJson)  List<int> baseRgbw, @JsonKey(name: 'shade', fromJson: _rgbwFromJson)  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs,  int fwVersion,  String fwChannel,  int otaState,  String? otaSendingTo,  bool near)?  $default,) {final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that.name,_that.lampId,_that.rssi,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs,_that.fwVersion,_that.fwChannel,_that.otaState,_that.otaSendingTo,_that.near);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _LampNearbyPeer implements LampNearbyPeer {
  const _LampNearbyPeer({required this.name, this.lampId = '', this.rssi = -127, @JsonKey(name: 'base', fromJson: _rgbwFromJson) final  List<int> baseRgbw = const <int>[0, 0, 0, 0], @JsonKey(name: 'shade', fromJson: _rgbwFromJson) final  List<int> shadeRgbw = const <int>[0, 0, 0, 0], this.viaBle = false, this.viaEspNow = false, this.lastSeenMs = 0, this.fwVersion = 0, this.fwChannel = '', this.otaState = 0, this.otaSendingTo, this.near = false}): _baseRgbw = baseRgbw,_shadeRgbw = shadeRgbw;
  factory _LampNearbyPeer.fromJson(Map<String, dynamic> json) => _$LampNearbyPeerFromJson(json);

@override final  String name;
/// Raw mesh MAC, uppercase colon-hex. Empty if the lamp firmware
/// predates the lampId emit.
@override@JsonKey() final  String lampId;
/// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
/// `-127` means "no reading yet" (older firmware that doesn't emit
/// RSSI, or a fresh peer not yet seen via BLE).
@override@JsonKey() final  int rssi;
/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
 final  List<int> _baseRgbw;
/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
@override@JsonKey(name: 'base', fromJson: _rgbwFromJson) List<int> get baseRgbw {
  if (_baseRgbw is EqualUnmodifiableListView) return _baseRgbw;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_baseRgbw);
}

 final  List<int> _shadeRgbw;
@override@JsonKey(name: 'shade', fromJson: _rgbwFromJson) List<int> get shadeRgbw {
  if (_shadeRgbw is EqualUnmodifiableListView) return _shadeRgbw;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_shadeRgbw);
}

@override@JsonKey() final  bool viaBle;
@override@JsonKey() final  bool viaEspNow;
@override@JsonKey() final  int lastSeenMs;
/// Packed semver (major<<16 | minor<<8 | patch). 0 = unknown/legacy
/// peer (the lamp omits it when zero).
@override@JsonKey() final  int fwVersion;
/// `{type}-{channel}` slot, e.g. "standard-beta". Empty on legacy
/// firmware that predates the emit.
@override@JsonKey() final  String fwChannel;
/// OTA state: 0=idle, 1=sending, 2=receiving. Omitted by the lamp
/// when idle.
@override@JsonKey() final  int otaState;
/// Mesh MAC (uppercase colon-hex) of the peer this lamp is OTA-sending
/// to. Null unless the lamp is Sending. Resolve against inventory by
/// `lampId` to name the receiver, which is HELLO-silent during its OTA.
@override final  String? otaSendingTo;
/// Firmware-computed proximity: true = near, false = far. The lamp
/// derives this from its own RSSI tiers; the app just renders it.
@override@JsonKey() final  bool near;

/// Create a copy of LampNearbyPeer
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$LampNearbyPeerCopyWith<_LampNearbyPeer> get copyWith => __$LampNearbyPeerCopyWithImpl<_LampNearbyPeer>(this, _$identity);

@override
Map<String, dynamic> toJson() {
  return _$LampNearbyPeerToJson(this, );
}

@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _LampNearbyPeer&&(identical(other.name, name) || other.name == name)&&(identical(other.lampId, lampId) || other.lampId == lampId)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&const DeepCollectionEquality().equals(other._baseRgbw, _baseRgbw)&&const DeepCollectionEquality().equals(other._shadeRgbw, _shadeRgbw)&&(identical(other.viaBle, viaBle) || other.viaBle == viaBle)&&(identical(other.viaEspNow, viaEspNow) || other.viaEspNow == viaEspNow)&&(identical(other.lastSeenMs, lastSeenMs) || other.lastSeenMs == lastSeenMs)&&(identical(other.fwVersion, fwVersion) || other.fwVersion == fwVersion)&&(identical(other.fwChannel, fwChannel) || other.fwChannel == fwChannel)&&(identical(other.otaState, otaState) || other.otaState == otaState)&&(identical(other.otaSendingTo, otaSendingTo) || other.otaSendingTo == otaSendingTo)&&(identical(other.near, near) || other.near == near));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,name,lampId,rssi,const DeepCollectionEquality().hash(_baseRgbw),const DeepCollectionEquality().hash(_shadeRgbw),viaBle,viaEspNow,lastSeenMs,fwVersion,fwChannel,otaState,otaSendingTo,near);

@override
String toString() {
  return 'LampNearbyPeer(name: $name, lampId: $lampId, rssi: $rssi, baseRgbw: $baseRgbw, shadeRgbw: $shadeRgbw, viaBle: $viaBle, viaEspNow: $viaEspNow, lastSeenMs: $lastSeenMs, fwVersion: $fwVersion, fwChannel: $fwChannel, otaState: $otaState, otaSendingTo: $otaSendingTo, near: $near)';
}


}

/// @nodoc
abstract mixin class _$LampNearbyPeerCopyWith<$Res> implements $LampNearbyPeerCopyWith<$Res> {
  factory _$LampNearbyPeerCopyWith(_LampNearbyPeer value, $Res Function(_LampNearbyPeer) _then) = __$LampNearbyPeerCopyWithImpl;
@override @useResult
$Res call({
 String name, String lampId, int rssi,@JsonKey(name: 'base', fromJson: _rgbwFromJson) List<int> baseRgbw,@JsonKey(name: 'shade', fromJson: _rgbwFromJson) List<int> shadeRgbw, bool viaBle, bool viaEspNow, int lastSeenMs, int fwVersion, String fwChannel, int otaState, String? otaSendingTo, bool near
});




}
/// @nodoc
class __$LampNearbyPeerCopyWithImpl<$Res>
    implements _$LampNearbyPeerCopyWith<$Res> {
  __$LampNearbyPeerCopyWithImpl(this._self, this._then);

  final _LampNearbyPeer _self;
  final $Res Function(_LampNearbyPeer) _then;

/// Create a copy of LampNearbyPeer
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? name = null,Object? lampId = null,Object? rssi = null,Object? baseRgbw = null,Object? shadeRgbw = null,Object? viaBle = null,Object? viaEspNow = null,Object? lastSeenMs = null,Object? fwVersion = null,Object? fwChannel = null,Object? otaState = null,Object? otaSendingTo = freezed,Object? near = null,}) {
  return _then(_LampNearbyPeer(
name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,lampId: null == lampId ? _self.lampId : lampId // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,baseRgbw: null == baseRgbw ? _self._baseRgbw : baseRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,shadeRgbw: null == shadeRgbw ? _self._shadeRgbw : shadeRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,viaBle: null == viaBle ? _self.viaBle : viaBle // ignore: cast_nullable_to_non_nullable
as bool,viaEspNow: null == viaEspNow ? _self.viaEspNow : viaEspNow // ignore: cast_nullable_to_non_nullable
as bool,lastSeenMs: null == lastSeenMs ? _self.lastSeenMs : lastSeenMs // ignore: cast_nullable_to_non_nullable
as int,fwVersion: null == fwVersion ? _self.fwVersion : fwVersion // ignore: cast_nullable_to_non_nullable
as int,fwChannel: null == fwChannel ? _self.fwChannel : fwChannel // ignore: cast_nullable_to_non_nullable
as String,otaState: null == otaState ? _self.otaState : otaState // ignore: cast_nullable_to_non_nullable
as int,otaSendingTo: freezed == otaSendingTo ? _self.otaSendingTo : otaSendingTo // ignore: cast_nullable_to_non_nullable
as String?,near: null == near ? _self.near : near // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}


}

// dart format on
