// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'add_lamp_state.dart';

// **************************************************************************
// JsonSerializableGenerator
// **************************************************************************

_AddLampState _$AddLampStateFromJson(Map<String, dynamic> json) =>
    _AddLampState(
      step:
          $enumDecodeNullable(_$AddLampStepEnumMap, json['step']) ??
          AddLampStep.scan,
      deviceId: json['deviceId'] as String? ?? '',
      name: json['name'] as String? ?? '',
      password: json['password'] as String? ?? '',
      baseRgb: (json['baseRgb'] as num?)?.toInt() ?? 0,
      shadeRgb: (json['shadeRgb'] as num?)?.toInt() ?? 0,
      status:
          $enumDecodeNullable(_$AddLampStatusEnumMap, json['status']) ??
          AddLampStatus.idle,
      error:
          $enumDecodeNullable(_$AddLampErrorEnumMap, json['error']) ??
          AddLampError.none,
      errorMessage: json['errorMessage'] as String?,
    );

Map<String, dynamic> _$AddLampStateToJson(_AddLampState instance) =>
    <String, dynamic>{
      'step': _$AddLampStepEnumMap[instance.step]!,
      'deviceId': instance.deviceId,
      'name': instance.name,
      'password': instance.password,
      'baseRgb': instance.baseRgb,
      'shadeRgb': instance.shadeRgb,
      'status': _$AddLampStatusEnumMap[instance.status]!,
      'error': _$AddLampErrorEnumMap[instance.error]!,
      'errorMessage': instance.errorMessage,
    };

const _$AddLampStepEnumMap = {
  AddLampStep.scan: 'scan',
  AddLampStep.adoptConfirm: 'adoptConfirm',
  AddLampStep.name: 'name',
  AddLampStep.password: 'password',
  AddLampStep.verifying: 'verifying',
  AddLampStep.done: 'done',
};

const _$AddLampStatusEnumMap = {
  AddLampStatus.idle: 'idle',
  AddLampStatus.working: 'working',
  AddLampStatus.ready: 'ready',
  AddLampStatus.error: 'error',
};

const _$AddLampErrorEnumMap = {
  AddLampError.none: 'none',
  AddLampError.wrongPassword: 'wrongPassword',
  AddLampError.claimFailed: 'claimFailed',
  AddLampError.connectFailed: 'connectFailed',
};
