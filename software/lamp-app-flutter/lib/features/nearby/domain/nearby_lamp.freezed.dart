// GENERATED CODE - DO NOT MODIFY BY HAND
// coverage:ignore-file
// ignore_for_file: type=lint
// ignore_for_file: unused_element, deprecated_member_use, deprecated_member_use_from_same_package, use_function_type_syntax_for_parameters, unnecessary_const, avoid_init_to_null, invalid_override_different_default_values_named, prefer_expression_function_bodies, annotate_overrides, invalid_annotation_target, unnecessary_question_mark

part of 'nearby_lamp.dart';

// **************************************************************************
// FreezedGenerator
// **************************************************************************

// dart format off
T _$identity<T>(T value) => value;

/// @nodoc
mixin _$NearbyLamp {

 String get id; String get name; int get rssi; List<String> get serviceUuids; int get baseRgb; int get shadeRgb; int get lastSeenEpochMs;/// True iff this lamp's firmware advertises the version byte —
/// i.e. speaks the app's mesh protocol and is fully app-
/// controllable. Drives the BT-only routing decision in
/// MyLampsScreen and the `mesh` vs `bluetooth` status dot. v1
/// firmware (legacy BT-only) and transitional pre-shade-restore
/// v2 builds both get `false`.
 bool get isMesh;/// True once the lamp has been claimed/set up — capability bit 0x04 in
/// the advertisement. Drives the adopt-wizard vs one-tap-add routing.
 bool get configured;
/// Create a copy of NearbyLamp
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$NearbyLampCopyWith<NearbyLamp> get copyWith => _$NearbyLampCopyWithImpl<NearbyLamp>(this as NearbyLamp, _$identity);

  /// Serializes this NearbyLamp to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is NearbyLamp&&(identical(other.id, id) || other.id == id)&&(identical(other.name, name) || other.name == name)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&const DeepCollectionEquality().equals(other.serviceUuids, serviceUuids)&&(identical(other.baseRgb, baseRgb) || other.baseRgb == baseRgb)&&(identical(other.shadeRgb, shadeRgb) || other.shadeRgb == shadeRgb)&&(identical(other.lastSeenEpochMs, lastSeenEpochMs) || other.lastSeenEpochMs == lastSeenEpochMs)&&(identical(other.isMesh, isMesh) || other.isMesh == isMesh)&&(identical(other.configured, configured) || other.configured == configured));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,id,name,rssi,const DeepCollectionEquality().hash(serviceUuids),baseRgb,shadeRgb,lastSeenEpochMs,isMesh,configured);

@override
String toString() {
  return 'NearbyLamp(id: $id, name: $name, rssi: $rssi, serviceUuids: $serviceUuids, baseRgb: $baseRgb, shadeRgb: $shadeRgb, lastSeenEpochMs: $lastSeenEpochMs, isMesh: $isMesh, configured: $configured)';
}


}

/// @nodoc
abstract mixin class $NearbyLampCopyWith<$Res>  {
  factory $NearbyLampCopyWith(NearbyLamp value, $Res Function(NearbyLamp) _then) = _$NearbyLampCopyWithImpl;
@useResult
$Res call({
 String id, String name, int rssi, List<String> serviceUuids, int baseRgb, int shadeRgb, int lastSeenEpochMs, bool isMesh, bool configured
});




}
/// @nodoc
class _$NearbyLampCopyWithImpl<$Res>
    implements $NearbyLampCopyWith<$Res> {
  _$NearbyLampCopyWithImpl(this._self, this._then);

  final NearbyLamp _self;
  final $Res Function(NearbyLamp) _then;

/// Create a copy of NearbyLamp
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? id = null,Object? name = null,Object? rssi = null,Object? serviceUuids = null,Object? baseRgb = null,Object? shadeRgb = null,Object? lastSeenEpochMs = null,Object? isMesh = null,Object? configured = null,}) {
  return _then(_self.copyWith(
id: null == id ? _self.id : id // ignore: cast_nullable_to_non_nullable
as String,name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,serviceUuids: null == serviceUuids ? _self.serviceUuids : serviceUuids // ignore: cast_nullable_to_non_nullable
as List<String>,baseRgb: null == baseRgb ? _self.baseRgb : baseRgb // ignore: cast_nullable_to_non_nullable
as int,shadeRgb: null == shadeRgb ? _self.shadeRgb : shadeRgb // ignore: cast_nullable_to_non_nullable
as int,lastSeenEpochMs: null == lastSeenEpochMs ? _self.lastSeenEpochMs : lastSeenEpochMs // ignore: cast_nullable_to_non_nullable
as int,isMesh: null == isMesh ? _self.isMesh : isMesh // ignore: cast_nullable_to_non_nullable
as bool,configured: null == configured ? _self.configured : configured // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}

}


/// Adds pattern-matching-related methods to [NearbyLamp].
extension NearbyLampPatterns on NearbyLamp {
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

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _NearbyLamp value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _NearbyLamp() when $default != null:
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

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _NearbyLamp value)  $default,){
final _that = this;
switch (_that) {
case _NearbyLamp():
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

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _NearbyLamp value)?  $default,){
final _that = this;
switch (_that) {
case _NearbyLamp() when $default != null:
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( String id,  String name,  int rssi,  List<String> serviceUuids,  int baseRgb,  int shadeRgb,  int lastSeenEpochMs,  bool isMesh,  bool configured)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _NearbyLamp() when $default != null:
return $default(_that.id,_that.name,_that.rssi,_that.serviceUuids,_that.baseRgb,_that.shadeRgb,_that.lastSeenEpochMs,_that.isMesh,_that.configured);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( String id,  String name,  int rssi,  List<String> serviceUuids,  int baseRgb,  int shadeRgb,  int lastSeenEpochMs,  bool isMesh,  bool configured)  $default,) {final _that = this;
switch (_that) {
case _NearbyLamp():
return $default(_that.id,_that.name,_that.rssi,_that.serviceUuids,_that.baseRgb,_that.shadeRgb,_that.lastSeenEpochMs,_that.isMesh,_that.configured);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( String id,  String name,  int rssi,  List<String> serviceUuids,  int baseRgb,  int shadeRgb,  int lastSeenEpochMs,  bool isMesh,  bool configured)?  $default,) {final _that = this;
switch (_that) {
case _NearbyLamp() when $default != null:
return $default(_that.id,_that.name,_that.rssi,_that.serviceUuids,_that.baseRgb,_that.shadeRgb,_that.lastSeenEpochMs,_that.isMesh,_that.configured);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _NearbyLamp extends NearbyLamp {
  const _NearbyLamp({required this.id, required this.name, required this.rssi, required final  List<String> serviceUuids, required this.baseRgb, required this.shadeRgb, required this.lastSeenEpochMs, this.isMesh = false, this.configured = false}): _serviceUuids = serviceUuids,super._();
  factory _NearbyLamp.fromJson(Map<String, dynamic> json) => _$NearbyLampFromJson(json);

@override final  String id;
@override final  String name;
@override final  int rssi;
 final  List<String> _serviceUuids;
@override List<String> get serviceUuids {
  if (_serviceUuids is EqualUnmodifiableListView) return _serviceUuids;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_serviceUuids);
}

@override final  int baseRgb;
@override final  int shadeRgb;
@override final  int lastSeenEpochMs;
/// True iff this lamp's firmware advertises the version byte —
/// i.e. speaks the app's mesh protocol and is fully app-
/// controllable. Drives the BT-only routing decision in
/// MyLampsScreen and the `mesh` vs `bluetooth` status dot. v1
/// firmware (legacy BT-only) and transitional pre-shade-restore
/// v2 builds both get `false`.
@override@JsonKey() final  bool isMesh;
/// True once the lamp has been claimed/set up — capability bit 0x04 in
/// the advertisement. Drives the adopt-wizard vs one-tap-add routing.
@override@JsonKey() final  bool configured;

/// Create a copy of NearbyLamp
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$NearbyLampCopyWith<_NearbyLamp> get copyWith => __$NearbyLampCopyWithImpl<_NearbyLamp>(this, _$identity);

@override
Map<String, dynamic> toJson() {
  return _$NearbyLampToJson(this, );
}

@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _NearbyLamp&&(identical(other.id, id) || other.id == id)&&(identical(other.name, name) || other.name == name)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&const DeepCollectionEquality().equals(other._serviceUuids, _serviceUuids)&&(identical(other.baseRgb, baseRgb) || other.baseRgb == baseRgb)&&(identical(other.shadeRgb, shadeRgb) || other.shadeRgb == shadeRgb)&&(identical(other.lastSeenEpochMs, lastSeenEpochMs) || other.lastSeenEpochMs == lastSeenEpochMs)&&(identical(other.isMesh, isMesh) || other.isMesh == isMesh)&&(identical(other.configured, configured) || other.configured == configured));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,id,name,rssi,const DeepCollectionEquality().hash(_serviceUuids),baseRgb,shadeRgb,lastSeenEpochMs,isMesh,configured);

@override
String toString() {
  return 'NearbyLamp(id: $id, name: $name, rssi: $rssi, serviceUuids: $serviceUuids, baseRgb: $baseRgb, shadeRgb: $shadeRgb, lastSeenEpochMs: $lastSeenEpochMs, isMesh: $isMesh, configured: $configured)';
}


}

/// @nodoc
abstract mixin class _$NearbyLampCopyWith<$Res> implements $NearbyLampCopyWith<$Res> {
  factory _$NearbyLampCopyWith(_NearbyLamp value, $Res Function(_NearbyLamp) _then) = __$NearbyLampCopyWithImpl;
@override @useResult
$Res call({
 String id, String name, int rssi, List<String> serviceUuids, int baseRgb, int shadeRgb, int lastSeenEpochMs, bool isMesh, bool configured
});




}
/// @nodoc
class __$NearbyLampCopyWithImpl<$Res>
    implements _$NearbyLampCopyWith<$Res> {
  __$NearbyLampCopyWithImpl(this._self, this._then);

  final _NearbyLamp _self;
  final $Res Function(_NearbyLamp) _then;

/// Create a copy of NearbyLamp
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? id = null,Object? name = null,Object? rssi = null,Object? serviceUuids = null,Object? baseRgb = null,Object? shadeRgb = null,Object? lastSeenEpochMs = null,Object? isMesh = null,Object? configured = null,}) {
  return _then(_NearbyLamp(
id: null == id ? _self.id : id // ignore: cast_nullable_to_non_nullable
as String,name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,serviceUuids: null == serviceUuids ? _self._serviceUuids : serviceUuids // ignore: cast_nullable_to_non_nullable
as List<String>,baseRgb: null == baseRgb ? _self.baseRgb : baseRgb // ignore: cast_nullable_to_non_nullable
as int,shadeRgb: null == shadeRgb ? _self.shadeRgb : shadeRgb // ignore: cast_nullable_to_non_nullable
as int,lastSeenEpochMs: null == lastSeenEpochMs ? _self.lastSeenEpochMs : lastSeenEpochMs // ignore: cast_nullable_to_non_nullable
as int,isMesh: null == isMesh ? _self.isMesh : isMesh // ignore: cast_nullable_to_non_nullable
as bool,configured: null == configured ? _self.configured : configured // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}


}

// dart format on
