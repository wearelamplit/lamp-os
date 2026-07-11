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

 String get name;/// Canonical-form colon-hex BD_ADDR. Empty if the lamp firmware
/// predates the bdAddr emit.
 String get bdAddr;/// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
/// `-127` means "no reading yet" (older firmware that doesn't emit
/// RSSI, or a fresh peer not yet seen via BLE).
 int get rssi;/// Proximity bucket: 0=Near, 1=Around, 2=Far. Default 2 keeps legacy
/// peers in a safe "Far" bucket rather than mis-classifying them as Near.
 int get proximity;/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
@JsonKey(name: 'base') List<int> get baseRgbw;@JsonKey(name: 'shade') List<int> get shadeRgbw; bool get viaBle; bool get viaEspNow; int get lastSeenMs;
/// Create a copy of LampNearbyPeer
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$LampNearbyPeerCopyWith<LampNearbyPeer> get copyWith => _$LampNearbyPeerCopyWithImpl<LampNearbyPeer>(this as LampNearbyPeer, _$identity);

  /// Serializes this LampNearbyPeer to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is LampNearbyPeer&&(identical(other.name, name) || other.name == name)&&(identical(other.bdAddr, bdAddr) || other.bdAddr == bdAddr)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&(identical(other.proximity, proximity) || other.proximity == proximity)&&const DeepCollectionEquality().equals(other.baseRgbw, baseRgbw)&&const DeepCollectionEquality().equals(other.shadeRgbw, shadeRgbw)&&(identical(other.viaBle, viaBle) || other.viaBle == viaBle)&&(identical(other.viaEspNow, viaEspNow) || other.viaEspNow == viaEspNow)&&(identical(other.lastSeenMs, lastSeenMs) || other.lastSeenMs == lastSeenMs));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,name,bdAddr,rssi,proximity,const DeepCollectionEquality().hash(baseRgbw),const DeepCollectionEquality().hash(shadeRgbw),viaBle,viaEspNow,lastSeenMs);

@override
String toString() {
  return 'LampNearbyPeer(name: $name, bdAddr: $bdAddr, rssi: $rssi, proximity: $proximity, baseRgbw: $baseRgbw, shadeRgbw: $shadeRgbw, viaBle: $viaBle, viaEspNow: $viaEspNow, lastSeenMs: $lastSeenMs)';
}


}

/// @nodoc
abstract mixin class $LampNearbyPeerCopyWith<$Res>  {
  factory $LampNearbyPeerCopyWith(LampNearbyPeer value, $Res Function(LampNearbyPeer) _then) = _$LampNearbyPeerCopyWithImpl;
@useResult
$Res call({
 String name, String bdAddr, int rssi, int proximity,@JsonKey(name: 'base') List<int> baseRgbw,@JsonKey(name: 'shade') List<int> shadeRgbw, bool viaBle, bool viaEspNow, int lastSeenMs
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
@pragma('vm:prefer-inline') @override $Res call({Object? name = null,Object? bdAddr = null,Object? rssi = null,Object? proximity = null,Object? baseRgbw = null,Object? shadeRgbw = null,Object? viaBle = null,Object? viaEspNow = null,Object? lastSeenMs = null,}) {
  return _then(_self.copyWith(
name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,bdAddr: null == bdAddr ? _self.bdAddr : bdAddr // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,proximity: null == proximity ? _self.proximity : proximity // ignore: cast_nullable_to_non_nullable
as int,baseRgbw: null == baseRgbw ? _self.baseRgbw : baseRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,shadeRgbw: null == shadeRgbw ? _self.shadeRgbw : shadeRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,viaBle: null == viaBle ? _self.viaBle : viaBle // ignore: cast_nullable_to_non_nullable
as bool,viaEspNow: null == viaEspNow ? _self.viaEspNow : viaEspNow // ignore: cast_nullable_to_non_nullable
as bool,lastSeenMs: null == lastSeenMs ? _self.lastSeenMs : lastSeenMs // ignore: cast_nullable_to_non_nullable
as int,
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( String name,  String bdAddr,  int rssi,  int proximity, @JsonKey(name: 'base')  List<int> baseRgbw, @JsonKey(name: 'shade')  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that.name,_that.bdAddr,_that.rssi,_that.proximity,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( String name,  String bdAddr,  int rssi,  int proximity, @JsonKey(name: 'base')  List<int> baseRgbw, @JsonKey(name: 'shade')  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs)  $default,) {final _that = this;
switch (_that) {
case _LampNearbyPeer():
return $default(_that.name,_that.bdAddr,_that.rssi,_that.proximity,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( String name,  String bdAddr,  int rssi,  int proximity, @JsonKey(name: 'base')  List<int> baseRgbw, @JsonKey(name: 'shade')  List<int> shadeRgbw,  bool viaBle,  bool viaEspNow,  int lastSeenMs)?  $default,) {final _that = this;
switch (_that) {
case _LampNearbyPeer() when $default != null:
return $default(_that.name,_that.bdAddr,_that.rssi,_that.proximity,_that.baseRgbw,_that.shadeRgbw,_that.viaBle,_that.viaEspNow,_that.lastSeenMs);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _LampNearbyPeer implements LampNearbyPeer {
  const _LampNearbyPeer({required this.name, this.bdAddr = '', this.rssi = -127, this.proximity = 2, @JsonKey(name: 'base') final  List<int> baseRgbw = const <int>[0, 0, 0, 0], @JsonKey(name: 'shade') final  List<int> shadeRgbw = const <int>[0, 0, 0, 0], this.viaBle = false, this.viaEspNow = false, this.lastSeenMs = 0}): _baseRgbw = baseRgbw,_shadeRgbw = shadeRgbw;
  factory _LampNearbyPeer.fromJson(Map<String, dynamic> json) => _$LampNearbyPeerFromJson(json);

@override final  String name;
/// Canonical-form colon-hex BD_ADDR. Empty if the lamp firmware
/// predates the bdAddr emit.
@override@JsonKey() final  String bdAddr;
/// Raw BLE-scan RSSI in dBm as observed by the connected lamp.
/// `-127` means "no reading yet" (older firmware that doesn't emit
/// RSSI, or a fresh peer not yet seen via BLE).
@override@JsonKey() final  int rssi;
/// Proximity bucket: 0=Near, 1=Around, 2=Far. Default 2 keeps legacy
/// peers in a safe "Far" bucket rather than mis-classifying them as Near.
@override@JsonKey() final  int proximity;
/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
 final  List<int> _baseRgbw;
/// 4-channel RGBW for the lamp's base and shade. Used to render
/// the lamp icon next to the row.
@override@JsonKey(name: 'base') List<int> get baseRgbw {
  if (_baseRgbw is EqualUnmodifiableListView) return _baseRgbw;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_baseRgbw);
}

 final  List<int> _shadeRgbw;
@override@JsonKey(name: 'shade') List<int> get shadeRgbw {
  if (_shadeRgbw is EqualUnmodifiableListView) return _shadeRgbw;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_shadeRgbw);
}

@override@JsonKey() final  bool viaBle;
@override@JsonKey() final  bool viaEspNow;
@override@JsonKey() final  int lastSeenMs;

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
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _LampNearbyPeer&&(identical(other.name, name) || other.name == name)&&(identical(other.bdAddr, bdAddr) || other.bdAddr == bdAddr)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&(identical(other.proximity, proximity) || other.proximity == proximity)&&const DeepCollectionEquality().equals(other._baseRgbw, _baseRgbw)&&const DeepCollectionEquality().equals(other._shadeRgbw, _shadeRgbw)&&(identical(other.viaBle, viaBle) || other.viaBle == viaBle)&&(identical(other.viaEspNow, viaEspNow) || other.viaEspNow == viaEspNow)&&(identical(other.lastSeenMs, lastSeenMs) || other.lastSeenMs == lastSeenMs));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,name,bdAddr,rssi,proximity,const DeepCollectionEquality().hash(_baseRgbw),const DeepCollectionEquality().hash(_shadeRgbw),viaBle,viaEspNow,lastSeenMs);

@override
String toString() {
  return 'LampNearbyPeer(name: $name, bdAddr: $bdAddr, rssi: $rssi, proximity: $proximity, baseRgbw: $baseRgbw, shadeRgbw: $shadeRgbw, viaBle: $viaBle, viaEspNow: $viaEspNow, lastSeenMs: $lastSeenMs)';
}


}

/// @nodoc
abstract mixin class _$LampNearbyPeerCopyWith<$Res> implements $LampNearbyPeerCopyWith<$Res> {
  factory _$LampNearbyPeerCopyWith(_LampNearbyPeer value, $Res Function(_LampNearbyPeer) _then) = __$LampNearbyPeerCopyWithImpl;
@override @useResult
$Res call({
 String name, String bdAddr, int rssi, int proximity,@JsonKey(name: 'base') List<int> baseRgbw,@JsonKey(name: 'shade') List<int> shadeRgbw, bool viaBle, bool viaEspNow, int lastSeenMs
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
@override @pragma('vm:prefer-inline') $Res call({Object? name = null,Object? bdAddr = null,Object? rssi = null,Object? proximity = null,Object? baseRgbw = null,Object? shadeRgbw = null,Object? viaBle = null,Object? viaEspNow = null,Object? lastSeenMs = null,}) {
  return _then(_LampNearbyPeer(
name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,bdAddr: null == bdAddr ? _self.bdAddr : bdAddr // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,proximity: null == proximity ? _self.proximity : proximity // ignore: cast_nullable_to_non_nullable
as int,baseRgbw: null == baseRgbw ? _self._baseRgbw : baseRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,shadeRgbw: null == shadeRgbw ? _self._shadeRgbw : shadeRgbw // ignore: cast_nullable_to_non_nullable
as List<int>,viaBle: null == viaBle ? _self.viaBle : viaBle // ignore: cast_nullable_to_non_nullable
as bool,viaEspNow: null == viaEspNow ? _self.viaEspNow : viaEspNow // ignore: cast_nullable_to_non_nullable
as bool,lastSeenMs: null == lastSeenMs ? _self.lastSeenMs : lastSeenMs // ignore: cast_nullable_to_non_nullable
as int,
  ));
}


}

// dart format on
