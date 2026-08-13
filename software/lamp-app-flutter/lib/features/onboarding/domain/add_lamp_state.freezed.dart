// GENERATED CODE - DO NOT MODIFY BY HAND
// coverage:ignore-file
// ignore_for_file: type=lint
// ignore_for_file: unused_element, deprecated_member_use, deprecated_member_use_from_same_package, use_function_type_syntax_for_parameters, unnecessary_const, avoid_init_to_null, invalid_override_different_default_values_named, prefer_expression_function_bodies, annotate_overrides, invalid_annotation_target, unnecessary_question_mark

part of 'add_lamp_state.dart';

// **************************************************************************
// FreezedGenerator
// **************************************************************************

// dart format off
T _$identity<T>(T value) => value;

/// @nodoc
mixin _$AddLampState {

 AddLampStep get step; String get deviceId; String get name; String get password; int get baseRgb; int get shadeRgb; AddLampStatus get status; AddLampError get error; String? get errorMessage;
/// Create a copy of AddLampState
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$AddLampStateCopyWith<AddLampState> get copyWith => _$AddLampStateCopyWithImpl<AddLampState>(this as AddLampState, _$identity);

  /// Serializes this AddLampState to a JSON map.
  Map<String, dynamic> toJson();


@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is AddLampState&&(identical(other.step, step) || other.step == step)&&(identical(other.deviceId, deviceId) || other.deviceId == deviceId)&&(identical(other.name, name) || other.name == name)&&(identical(other.password, password) || other.password == password)&&(identical(other.baseRgb, baseRgb) || other.baseRgb == baseRgb)&&(identical(other.shadeRgb, shadeRgb) || other.shadeRgb == shadeRgb)&&(identical(other.status, status) || other.status == status)&&(identical(other.error, error) || other.error == error)&&(identical(other.errorMessage, errorMessage) || other.errorMessage == errorMessage));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,step,deviceId,name,password,baseRgb,shadeRgb,status,error,errorMessage);

@override
String toString() {
  return 'AddLampState(step: $step, deviceId: $deviceId, name: $name, password: $password, baseRgb: $baseRgb, shadeRgb: $shadeRgb, status: $status, error: $error, errorMessage: $errorMessage)';
}


}

/// @nodoc
abstract mixin class $AddLampStateCopyWith<$Res>  {
  factory $AddLampStateCopyWith(AddLampState value, $Res Function(AddLampState) _then) = _$AddLampStateCopyWithImpl;
@useResult
$Res call({
 AddLampStep step, String deviceId, String name, String password, int baseRgb, int shadeRgb, AddLampStatus status, AddLampError error, String? errorMessage
});




}
/// @nodoc
class _$AddLampStateCopyWithImpl<$Res>
    implements $AddLampStateCopyWith<$Res> {
  _$AddLampStateCopyWithImpl(this._self, this._then);

  final AddLampState _self;
  final $Res Function(AddLampState) _then;

/// Create a copy of AddLampState
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? step = null,Object? deviceId = null,Object? name = null,Object? password = null,Object? baseRgb = null,Object? shadeRgb = null,Object? status = null,Object? error = null,Object? errorMessage = freezed,}) {
  return _then(_self.copyWith(
step: null == step ? _self.step : step // ignore: cast_nullable_to_non_nullable
as AddLampStep,deviceId: null == deviceId ? _self.deviceId : deviceId // ignore: cast_nullable_to_non_nullable
as String,name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,password: null == password ? _self.password : password // ignore: cast_nullable_to_non_nullable
as String,baseRgb: null == baseRgb ? _self.baseRgb : baseRgb // ignore: cast_nullable_to_non_nullable
as int,shadeRgb: null == shadeRgb ? _self.shadeRgb : shadeRgb // ignore: cast_nullable_to_non_nullable
as int,status: null == status ? _self.status : status // ignore: cast_nullable_to_non_nullable
as AddLampStatus,error: null == error ? _self.error : error // ignore: cast_nullable_to_non_nullable
as AddLampError,errorMessage: freezed == errorMessage ? _self.errorMessage : errorMessage // ignore: cast_nullable_to_non_nullable
as String?,
  ));
}

}


/// Adds pattern-matching-related methods to [AddLampState].
extension AddLampStatePatterns on AddLampState {
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

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _AddLampState value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _AddLampState() when $default != null:
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

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _AddLampState value)  $default,){
final _that = this;
switch (_that) {
case _AddLampState():
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

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _AddLampState value)?  $default,){
final _that = this;
switch (_that) {
case _AddLampState() when $default != null:
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( AddLampStep step,  String deviceId,  String name,  String password,  int baseRgb,  int shadeRgb,  AddLampStatus status,  AddLampError error,  String? errorMessage)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _AddLampState() when $default != null:
return $default(_that.step,_that.deviceId,_that.name,_that.password,_that.baseRgb,_that.shadeRgb,_that.status,_that.error,_that.errorMessage);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( AddLampStep step,  String deviceId,  String name,  String password,  int baseRgb,  int shadeRgb,  AddLampStatus status,  AddLampError error,  String? errorMessage)  $default,) {final _that = this;
switch (_that) {
case _AddLampState():
return $default(_that.step,_that.deviceId,_that.name,_that.password,_that.baseRgb,_that.shadeRgb,_that.status,_that.error,_that.errorMessage);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( AddLampStep step,  String deviceId,  String name,  String password,  int baseRgb,  int shadeRgb,  AddLampStatus status,  AddLampError error,  String? errorMessage)?  $default,) {final _that = this;
switch (_that) {
case _AddLampState() when $default != null:
return $default(_that.step,_that.deviceId,_that.name,_that.password,_that.baseRgb,_that.shadeRgb,_that.status,_that.error,_that.errorMessage);case _:
  return null;

}
}

}

/// @nodoc
@JsonSerializable()

class _AddLampState implements AddLampState {
  const _AddLampState({this.step = AddLampStep.scan, this.deviceId = '', this.name = '', this.password = '', this.baseRgb = 0, this.shadeRgb = 0, this.status = AddLampStatus.idle, this.error = AddLampError.none, this.errorMessage});
  factory _AddLampState.fromJson(Map<String, dynamic> json) => _$AddLampStateFromJson(json);

@override@JsonKey() final  AddLampStep step;
@override@JsonKey() final  String deviceId;
@override@JsonKey() final  String name;
@override@JsonKey() final  String password;
@override@JsonKey() final  int baseRgb;
@override@JsonKey() final  int shadeRgb;
@override@JsonKey() final  AddLampStatus status;
@override@JsonKey() final  AddLampError error;
@override final  String? errorMessage;

/// Create a copy of AddLampState
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$AddLampStateCopyWith<_AddLampState> get copyWith => __$AddLampStateCopyWithImpl<_AddLampState>(this, _$identity);

@override
Map<String, dynamic> toJson() {
  return _$AddLampStateToJson(this, );
}

@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _AddLampState&&(identical(other.step, step) || other.step == step)&&(identical(other.deviceId, deviceId) || other.deviceId == deviceId)&&(identical(other.name, name) || other.name == name)&&(identical(other.password, password) || other.password == password)&&(identical(other.baseRgb, baseRgb) || other.baseRgb == baseRgb)&&(identical(other.shadeRgb, shadeRgb) || other.shadeRgb == shadeRgb)&&(identical(other.status, status) || other.status == status)&&(identical(other.error, error) || other.error == error)&&(identical(other.errorMessage, errorMessage) || other.errorMessage == errorMessage));
}

@JsonKey(includeFromJson: false, includeToJson: false)
@override
int get hashCode => Object.hash(runtimeType,step,deviceId,name,password,baseRgb,shadeRgb,status,error,errorMessage);

@override
String toString() {
  return 'AddLampState(step: $step, deviceId: $deviceId, name: $name, password: $password, baseRgb: $baseRgb, shadeRgb: $shadeRgb, status: $status, error: $error, errorMessage: $errorMessage)';
}


}

/// @nodoc
abstract mixin class _$AddLampStateCopyWith<$Res> implements $AddLampStateCopyWith<$Res> {
  factory _$AddLampStateCopyWith(_AddLampState value, $Res Function(_AddLampState) _then) = __$AddLampStateCopyWithImpl;
@override @useResult
$Res call({
 AddLampStep step, String deviceId, String name, String password, int baseRgb, int shadeRgb, AddLampStatus status, AddLampError error, String? errorMessage
});




}
/// @nodoc
class __$AddLampStateCopyWithImpl<$Res>
    implements _$AddLampStateCopyWith<$Res> {
  __$AddLampStateCopyWithImpl(this._self, this._then);

  final _AddLampState _self;
  final $Res Function(_AddLampState) _then;

/// Create a copy of AddLampState
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? step = null,Object? deviceId = null,Object? name = null,Object? password = null,Object? baseRgb = null,Object? shadeRgb = null,Object? status = null,Object? error = null,Object? errorMessage = freezed,}) {
  return _then(_AddLampState(
step: null == step ? _self.step : step // ignore: cast_nullable_to_non_nullable
as AddLampStep,deviceId: null == deviceId ? _self.deviceId : deviceId // ignore: cast_nullable_to_non_nullable
as String,name: null == name ? _self.name : name // ignore: cast_nullable_to_non_nullable
as String,password: null == password ? _self.password : password // ignore: cast_nullable_to_non_nullable
as String,baseRgb: null == baseRgb ? _self.baseRgb : baseRgb // ignore: cast_nullable_to_non_nullable
as int,shadeRgb: null == shadeRgb ? _self.shadeRgb : shadeRgb // ignore: cast_nullable_to_non_nullable
as int,status: null == status ? _self.status : status // ignore: cast_nullable_to_non_nullable
as AddLampStatus,error: null == error ? _self.error : error // ignore: cast_nullable_to_non_nullable
as AddLampError,errorMessage: freezed == errorMessage ? _self.errorMessage : errorMessage // ignore: cast_nullable_to_non_nullable
as String?,
  ));
}


}

// dart format on
