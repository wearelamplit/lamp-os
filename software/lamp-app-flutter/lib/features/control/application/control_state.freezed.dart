// GENERATED CODE - DO NOT MODIFY BY HAND
// coverage:ignore-file
// ignore_for_file: type=lint
// ignore_for_file: unused_element, deprecated_member_use, deprecated_member_use_from_same_package, use_function_type_syntax_for_parameters, unnecessary_const, avoid_init_to_null, invalid_override_different_default_values_named, prefer_expression_function_bodies, annotate_overrides, invalid_annotation_target, unnecessary_question_mark

part of 'control_state.dart';

// **************************************************************************
// FreezedGenerator
// **************************************************************************

// dart format off
T _$identity<T>(T value) => value;
/// @nodoc
mixin _$ControlState {

 LampSection get lamp; BaseSection get base; ShadeSection get shade; HomeSection get home; ExpressionsSection get expressions; bool get connected; int get reconnectAttempt; bool get previewActive;
/// Create a copy of ControlState
/// with the given fields replaced by the non-null parameter values.
@JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
$ControlStateCopyWith<ControlState> get copyWith => _$ControlStateCopyWithImpl<ControlState>(this as ControlState, _$identity);



@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is ControlState&&(identical(other.lamp, lamp) || other.lamp == lamp)&&(identical(other.base, base) || other.base == base)&&(identical(other.shade, shade) || other.shade == shade)&&(identical(other.home, home) || other.home == home)&&(identical(other.expressions, expressions) || other.expressions == expressions)&&(identical(other.connected, connected) || other.connected == connected)&&(identical(other.reconnectAttempt, reconnectAttempt) || other.reconnectAttempt == reconnectAttempt)&&(identical(other.previewActive, previewActive) || other.previewActive == previewActive));
}


@override
int get hashCode => Object.hash(runtimeType,lamp,base,shade,home,expressions,connected,reconnectAttempt,previewActive);

@override
String toString() {
  return 'ControlState(lamp: $lamp, base: $base, shade: $shade, home: $home, expressions: $expressions, connected: $connected, reconnectAttempt: $reconnectAttempt, previewActive: $previewActive)';
}


}

/// @nodoc
abstract mixin class $ControlStateCopyWith<$Res>  {
  factory $ControlStateCopyWith(ControlState value, $Res Function(ControlState) _then) = _$ControlStateCopyWithImpl;
@useResult
$Res call({
 LampSection lamp, BaseSection base, ShadeSection shade, HomeSection home, ExpressionsSection expressions, bool connected, int reconnectAttempt, bool previewActive
});




}
/// @nodoc
class _$ControlStateCopyWithImpl<$Res>
    implements $ControlStateCopyWith<$Res> {
  _$ControlStateCopyWithImpl(this._self, this._then);

  final ControlState _self;
  final $Res Function(ControlState) _then;

/// Create a copy of ControlState
/// with the given fields replaced by the non-null parameter values.
@pragma('vm:prefer-inline') @override $Res call({Object? lamp = null,Object? base = null,Object? shade = null,Object? home = null,Object? expressions = null,Object? connected = null,Object? reconnectAttempt = null,Object? previewActive = null,}) {
  return _then(_self.copyWith(
lamp: null == lamp ? _self.lamp : lamp // ignore: cast_nullable_to_non_nullable
as LampSection,base: null == base ? _self.base : base // ignore: cast_nullable_to_non_nullable
as BaseSection,shade: null == shade ? _self.shade : shade // ignore: cast_nullable_to_non_nullable
as ShadeSection,home: null == home ? _self.home : home // ignore: cast_nullable_to_non_nullable
as HomeSection,expressions: null == expressions ? _self.expressions : expressions // ignore: cast_nullable_to_non_nullable
as ExpressionsSection,connected: null == connected ? _self.connected : connected // ignore: cast_nullable_to_non_nullable
as bool,reconnectAttempt: null == reconnectAttempt ? _self.reconnectAttempt : reconnectAttempt // ignore: cast_nullable_to_non_nullable
as int,previewActive: null == previewActive ? _self.previewActive : previewActive // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}

}


/// Adds pattern-matching-related methods to [ControlState].
extension ControlStatePatterns on ControlState {
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

@optionalTypeArgs TResult maybeMap<TResult extends Object?>(TResult Function( _ControlState value)?  $default,{required TResult orElse(),}){
final _that = this;
switch (_that) {
case _ControlState() when $default != null:
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

@optionalTypeArgs TResult map<TResult extends Object?>(TResult Function( _ControlState value)  $default,){
final _that = this;
switch (_that) {
case _ControlState():
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

@optionalTypeArgs TResult? mapOrNull<TResult extends Object?>(TResult? Function( _ControlState value)?  $default,){
final _that = this;
switch (_that) {
case _ControlState() when $default != null:
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

@optionalTypeArgs TResult maybeWhen<TResult extends Object?>(TResult Function( LampSection lamp,  BaseSection base,  ShadeSection shade,  HomeSection home,  ExpressionsSection expressions,  bool connected,  int reconnectAttempt,  bool previewActive)?  $default,{required TResult orElse(),}) {final _that = this;
switch (_that) {
case _ControlState() when $default != null:
return $default(_that.lamp,_that.base,_that.shade,_that.home,_that.expressions,_that.connected,_that.reconnectAttempt,_that.previewActive);case _:
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

@optionalTypeArgs TResult when<TResult extends Object?>(TResult Function( LampSection lamp,  BaseSection base,  ShadeSection shade,  HomeSection home,  ExpressionsSection expressions,  bool connected,  int reconnectAttempt,  bool previewActive)  $default,) {final _that = this;
switch (_that) {
case _ControlState():
return $default(_that.lamp,_that.base,_that.shade,_that.home,_that.expressions,_that.connected,_that.reconnectAttempt,_that.previewActive);case _:
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

@optionalTypeArgs TResult? whenOrNull<TResult extends Object?>(TResult? Function( LampSection lamp,  BaseSection base,  ShadeSection shade,  HomeSection home,  ExpressionsSection expressions,  bool connected,  int reconnectAttempt,  bool previewActive)?  $default,) {final _that = this;
switch (_that) {
case _ControlState() when $default != null:
return $default(_that.lamp,_that.base,_that.shade,_that.home,_that.expressions,_that.connected,_that.reconnectAttempt,_that.previewActive);case _:
  return null;

}
}

}

/// @nodoc


class _ControlState implements ControlState {
  const _ControlState({required this.lamp, required this.base, required this.shade, required this.home, required this.expressions, this.connected = true, this.reconnectAttempt = 0, this.previewActive = false});
  

@override final  LampSection lamp;
@override final  BaseSection base;
@override final  ShadeSection shade;
@override final  HomeSection home;
@override final  ExpressionsSection expressions;
@override@JsonKey() final  bool connected;
@override@JsonKey() final  int reconnectAttempt;
@override@JsonKey() final  bool previewActive;

/// Create a copy of ControlState
/// with the given fields replaced by the non-null parameter values.
@override @JsonKey(includeFromJson: false, includeToJson: false)
@pragma('vm:prefer-inline')
_$ControlStateCopyWith<_ControlState> get copyWith => __$ControlStateCopyWithImpl<_ControlState>(this, _$identity);



@override
bool operator ==(Object other) {
  return identical(this, other) || (other.runtimeType == runtimeType&&other is _ControlState&&(identical(other.lamp, lamp) || other.lamp == lamp)&&(identical(other.base, base) || other.base == base)&&(identical(other.shade, shade) || other.shade == shade)&&(identical(other.home, home) || other.home == home)&&(identical(other.expressions, expressions) || other.expressions == expressions)&&(identical(other.connected, connected) || other.connected == connected)&&(identical(other.reconnectAttempt, reconnectAttempt) || other.reconnectAttempt == reconnectAttempt)&&(identical(other.previewActive, previewActive) || other.previewActive == previewActive));
}


@override
int get hashCode => Object.hash(runtimeType,lamp,base,shade,home,expressions,connected,reconnectAttempt,previewActive);

@override
String toString() {
  return 'ControlState(lamp: $lamp, base: $base, shade: $shade, home: $home, expressions: $expressions, connected: $connected, reconnectAttempt: $reconnectAttempt, previewActive: $previewActive)';
}


}

/// @nodoc
abstract mixin class _$ControlStateCopyWith<$Res> implements $ControlStateCopyWith<$Res> {
  factory _$ControlStateCopyWith(_ControlState value, $Res Function(_ControlState) _then) = __$ControlStateCopyWithImpl;
@override @useResult
$Res call({
 LampSection lamp, BaseSection base, ShadeSection shade, HomeSection home, ExpressionsSection expressions, bool connected, int reconnectAttempt, bool previewActive
});




}
/// @nodoc
class __$ControlStateCopyWithImpl<$Res>
    implements _$ControlStateCopyWith<$Res> {
  __$ControlStateCopyWithImpl(this._self, this._then);

  final _ControlState _self;
  final $Res Function(_ControlState) _then;

/// Create a copy of ControlState
/// with the given fields replaced by the non-null parameter values.
@override @pragma('vm:prefer-inline') $Res call({Object? lamp = null,Object? base = null,Object? shade = null,Object? home = null,Object? expressions = null,Object? connected = null,Object? reconnectAttempt = null,Object? previewActive = null,}) {
  return _then(_ControlState(
lamp: null == lamp ? _self.lamp : lamp // ignore: cast_nullable_to_non_nullable
as LampSection,base: null == base ? _self.base : base // ignore: cast_nullable_to_non_nullable
as BaseSection,shade: null == shade ? _self.shade : shade // ignore: cast_nullable_to_non_nullable
as ShadeSection,home: null == home ? _self.home : home // ignore: cast_nullable_to_non_nullable
as HomeSection,expressions: null == expressions ? _self.expressions : expressions // ignore: cast_nullable_to_non_nullable
as ExpressionsSection,connected: null == connected ? _self.connected : connected // ignore: cast_nullable_to_non_nullable
as bool,reconnectAttempt: null == reconnectAttempt ? _self.reconnectAttempt : reconnectAttempt // ignore: cast_nullable_to_non_nullable
as int,previewActive: null == previewActive ? _self.previewActive : previewActive // ignore: cast_nullable_to_non_nullable
as bool,
  ));
}


}

// dart format on
