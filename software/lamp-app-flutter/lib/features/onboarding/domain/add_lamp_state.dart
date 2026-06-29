import 'package:freezed_annotation/freezed_annotation.dart';

part 'add_lamp_state.freezed.dart';
part 'add_lamp_state.g.dart';

enum AddLampStep { scan, adoptConfirm, name, password, verifying, done }

enum AddLampStatus { idle, working, error }

enum AddLampError { none, wrongPassword, claimFailed, connectFailed }

@freezed
abstract class AddLampState with _$AddLampState {
  const factory AddLampState({
    @Default(AddLampStep.scan) AddLampStep step,
    @Default('') String deviceId,
    @Default('') String name,
    @Default('') String password,
    @Default(AddLampStatus.idle) AddLampStatus status,
    @Default(AddLampError.none) AddLampError error,
    String? errorMessage,
  }) = _AddLampState;

  factory AddLampState.fromJson(Map<String, dynamic> json) =>
      _$AddLampStateFromJson(json);
}
