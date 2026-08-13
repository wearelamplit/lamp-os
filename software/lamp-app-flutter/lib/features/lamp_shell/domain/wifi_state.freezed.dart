// GENERATED CODE - DO NOT MODIFY BY HAND
// coverage:ignore-file
// ignore_for_file: type=lint
// ignore_for_file: unused_element, deprecated_member_use, deprecated_member_use_from_same_package, use_function_type_syntax_for_parameters, unnecessary_const, avoid_init_to_null, invalid_override_different_default_values_named, prefer_expression_function_bodies, annotate_overrides, invalid_annotation_target, unnecessary_question_mark

part of 'wifi_state.dart';

// **************************************************************************
// FreezedGenerator
// **************************************************************************

// dart format off
T _$identity<T>(T value) => value;

/// @nodoc
mixin _$WifiScanResult {

 String get ssid; int get rssi; bool get encrypted;
/// Create a copy of WifiScanResult
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$WifiScanResultCopyWith<WifiScanResult> get copyWith => _$WifiScanResultCopyWithImpl<WifiScanResult>(this as WifiScanResult, _$identity);

  /// Serializes this WifiScanResult to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is WifiScanResult&&(identical(other.ssid, ssid) || other.ssid == ssid)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&(identical(other.encrypted, encrypted) || other.encrypted == encrypted));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,ssid,rssi,encrypted);

@override
String toString() {
  return 'WifiScanResult(ssid: $ssid, rssi: $rssi, encrypted: $encrypted)';
}


}

/// @nodoc
abstract mixin class $WifiScanResultCopyWith<$Res>  {
  factory $WifiScanResultCopyWith(WifiScanResult value, $Res Function(WifiScanResult) _then) = _$WifiScanResultCopyWithImpl;
@useResult
$Res call({
 String ssid, int rssi, bool encrypted
});




}
/// @nodoc
class _$WifiScanResultCopyWithImpl<$Res>
    implements $WifiScanResultCopyWith<$Res> {
  _$WifiScanResultCopyWithImpl(this._self, this._then);

  final WifiScanResult _self;
  final $Res Function(WifiScanResult) _then;

/// Create a copy of WifiScanResult
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? ssid = null,Object? rssi = null,Object? encrypted = null,}) {
  return _then(_self.copyWith(
ssid: null == ssid ? _self.ssid : ssid // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,encrypted: null == encrypted ? _self.encrypted : encrypted // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}

}


/// Adds pattern-matching-related methods to [WifiScanResult].
extension WifiScanResultPatterns on WifiScanResult {
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

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _WifiScanResult value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _WifiScanResult() when $default != null:
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

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _WifiScanResult value)  $default,){
final _that = this;
switch (_that) {
case _WifiScanResult():
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

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _WifiScanResult value)?  $default,){
final _that = this;
switch (_that) {
case _WifiScanResult() when $default != null:
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( String ssid,  int rssi,  bool encrypted)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _WifiScanResult() when $default != null:
return $default(_that.ssid,_that.rssi,_that.encrypted);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( String ssid,  int rssi,  bool encrypted)  $default,) {final _that = this;
switch (_that) {
case _WifiScanResult():
return $default(_that.ssid,_that.rssi,_that.encrypted);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( String ssid,  int rssi,  bool encrypted)?  $default,) {final _that = this;
switch (_that) {
case _WifiScanResult() when $default != null:
return $default(_that.ssid,_that.rssi,_that.encrypted);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _WifiScanResult implements WifiScanResult {
  const _WifiScanResult({required this.ssid, required this.rssi, required this.encrypted});
  factory _WifiScanResult.fromJson(Map<String, dynamic> json) => _$WifiScanResultFromJson(json);

@override final  String ssid;
@override final  int rssi;
@override final  bool encrypted;

/// Create a copy of WifiScanResult
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$WifiScanResultCopyWith<_WifiScanResult> get copyWith => __$WifiScanResultCopyWithImpl<_WifiScanResult>(this, _$identity);

@override
Map<String, dynamic> toJson() {
  return _$WifiScanResultToJson(this, );
}

@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _WifiScanResult&&(identical(other.ssid, ssid) || other.ssid == ssid)&&(identical(other.rssi, rssi) || other.rssi == rssi)&&(identical(other.encrypted, encrypted) || other.encrypted == encrypted));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,ssid,rssi,encrypted);

@override
String toString() {
  return 'WifiScanResult(ssid: $ssid, rssi: $rssi, encrypted: $encrypted)';
}


}

/// @nodoc
abstract mixin class _$WifiScanResultCopyWith<$Res> implements $WifiScanResultCopyWith<$Res> {
  factory _$WifiScanResultCopyWith(_WifiScanResult value, $Res Function(_WifiScanResult) _then) = __$WifiScanResultCopyWithImpl;
@override @useResult
$Res call({
 String ssid, int rssi, bool encrypted
});




}
/// @nodoc
class __$WifiScanResultCopyWithImpl<$Res>
    implements _$WifiScanResultCopyWith<$Res> {
  __$WifiScanResultCopyWithImpl(this._self, this._then);

  final _WifiScanResult _self;
  final $Res Function(_WifiScanResult) _then;

/// Create a copy of WifiScanResult
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? ssid = null,Object? rssi = null,Object? encrypted = null,}) {
  return _then(_WifiScanResult(
ssid: null == ssid ? _self.ssid : ssid // ignore: cast_nullable_to_non_nullable
as String,rssi: null == rssi ? _self.rssi : rssi // ignore: cast_nullable_to_non_nullable
as int,encrypted: null == encrypted ? _self.encrypted : encrypted // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}


}


/// @nodoc
mixin _$WifiState {

 String get state; String? get ssid; String? get ip; String? get lastError; List<WifiScanResult> get scanResults;
/// Create a copy of WifiState
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$WifiStateCopyWith<WifiState> get copyWith => _$WifiStateCopyWithImpl<WifiState>(this as WifiState, _$identity);

  /// Serializes this WifiState to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is WifiState&&(identical(other.state, state) || other.state == state)&&(identical(other.ssid, ssid) || other.ssid == ssid)&&(identical(other.ip, ip) || other.ip == ip)&&(identical(other.lastError, lastError) || other.lastError == lastError)&&const DeepCollectionEquality().equals(other.scanResults, scanResults));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,state,ssid,ip,lastError,const DeepCollectionEquality().hash(scanResults));

@override
String toString() {
  return 'WifiState(state: $state, ssid: $ssid, ip: $ip, lastError: $lastError, scanResults: $scanResults)';
}


}

/// @nodoc
abstract mixin class $WifiStateCopyWith<$Res>  {
  factory $WifiStateCopyWith(WifiState value, $Res Function(WifiState) _then) = _$WifiStateCopyWithImpl;
@useResult
$Res call({
 String state, String? ssid, String? ip, String? lastError, List<WifiScanResult> scanResults
});




}
/// @nodoc
class _$WifiStateCopyWithImpl<$Res>
    implements $WifiStateCopyWith<$Res> {
  _$WifiStateCopyWithImpl(this._self, this._then);

  final WifiState _self;
  final $Res Function(WifiState) _then;

/// Create a copy of WifiState
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? state = null,Object? ssid = freezed,Object? ip = freezed,Object? lastError = freezed,Object? scanResults = null,}) {
  return _then(_self.copyWith(
state: null == state ? _self.state : state // ignore: cast_nullable_to_non_nullable
as String,ssid: freezed == ssid ? _self.ssid : ssid // ignore: cast_nullable_to_non_nullable
as String?,ip: freezed == ip ? _self.ip : ip // ignore: cast_nullable_to_non_nullable
as String?,lastError: freezed == lastError ? _self.lastError : lastError // ignore: cast_nullable_to_non_nullable
as String?,scanResults: null == scanResults ? _self.scanResults : scanResults // ignore: cast_nullable_to_non_nullable
as List<WifiScanResult>,
  ));
}

}


/// Adds pattern-matching-related methods to [WifiState].
extension WifiStatePatterns on WifiState {
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

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _WifiState value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _WifiState() when $default != null:
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

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _WifiState value)  $default,){
final _that = this;
switch (_that) {
case _WifiState():
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

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _WifiState value)?  $default,){
final _that = this;
switch (_that) {
case _WifiState() when $default != null:
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( String state,  String? ssid,  String? ip,  String? lastError,  List<WifiScanResult> scanResults)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _WifiState() when $default != null:
return $default(_that.state,_that.ssid,_that.ip,_that.lastError,_that.scanResults);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( String state,  String? ssid,  String? ip,  String? lastError,  List<WifiScanResult> scanResults)  $default,) {final _that = this;
switch (_that) {
case _WifiState():
return $default(_that.state,_that.ssid,_that.ip,_that.lastError,_that.scanResults);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( String state,  String? ssid,  String? ip,  String? lastError,  List<WifiScanResult> scanResults)?  $default,) {final _that = this;
switch (_that) {
case _WifiState() when $default != null:
return $default(_that.state,_that.ssid,_that.ip,_that.lastError,_that.scanResults);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _WifiState implements WifiState {
  const _WifiState({this.state = 'idle', this.ssid, this.ip, this.lastError, final  List<WifiScanResult> scanResults = const <WifiScanResult>[]}): _scanResults = scanResults;
  factory _WifiState.fromJson(Map<String, dynamic> json) => _$WifiStateFromJson(json);

@override@JsonKey() final  String state;
@override final  String? ssid;
@override final  String? ip;
@override final  String? lastError;
 final  List<WifiScanResult> _scanResults;
@override@JsonKey() List<WifiScanResult> get scanResults {
  if (_scanResults is EqualUnmodifiableListView) return _scanResults;
  // ignore: implicit_dynamic_type
  return EqualUnmodifiableListView(_scanResults);
}


/// Create a copy of WifiState
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$WifiStateCopyWith<_WifiState> get copyWith => __$WifiStateCopyWithImpl<_WifiState>(this, _$identity);

@override
Map<String, dynamic> toJson() {
  return _$WifiStateToJson(this, );
}

@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _WifiState&&(identical(other.state, state) || other.state == state)&&(identical(other.ssid, ssid) || other.ssid == ssid)&&(identical(other.ip, ip) || other.ip == ip)&&(identical(other.lastError, lastError) || other.lastError == lastError)&&const DeepCollectionEquality().equals(other._scanResults, _scanResults));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,state,ssid,ip,lastError,const DeepCollectionEquality().hash(_scanResults));

@override
String toString() {
  return 'WifiState(state: $state, ssid: $ssid, ip: $ip, lastError: $lastError, scanResults: $scanResults)';
}


}

/// @nodoc
abstract mixin class _$WifiStateCopyWith<$Res> implements $WifiStateCopyWith<$Res> {
  factory _$WifiStateCopyWith(_WifiState value, $Res Function(_WifiState) _then) = __$WifiStateCopyWithImpl;
@override @useResult
$Res call({
 String state, String? ssid, String? ip, String? lastError, List<WifiScanResult> scanResults
});




}
/// @nodoc
class __$WifiStateCopyWithImpl<$Res>
    implements _$WifiStateCopyWith<$Res> {
  __$WifiStateCopyWithImpl(this._self, this._then);

  final _WifiState _self;
  final $Res Function(_WifiState) _then;

/// Create a copy of WifiState
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? state = null,Object? ssid = freezed,Object? ip = freezed,Object? lastError = freezed,Object? scanResults = null,}) {
  return _then(_WifiState(
state: null == state ? _self.state : state // ignore: cast_nullable_to_non_nullable
as String,ssid: freezed == ssid ? _self.ssid : ssid // ignore: cast_nullable_to_non_nullable
as String?,ip: freezed == ip ? _self.ip : ip // ignore: cast_nullable_to_non_nullable
as String?,lastError: freezed == lastError ? _self.lastError : lastError // ignore: cast_nullable_to_non_nullable
as String?,scanResults: null == scanResults ? _self._scanResults : scanResults // ignore: cast_nullable_to_non_nullable
as List<WifiScanResult>,
  ));
}


}

// dart format on
