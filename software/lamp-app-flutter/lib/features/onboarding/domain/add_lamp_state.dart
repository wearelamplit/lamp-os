import 'package:freezed_annotation/freezed_annotation.dart';

part 'add_lamp_state.freezed.dart';
part 'add_lamp_state.g.dart';

enum AddLampStep { scan, adoptConfirm, name, password, verifying, done }

// `ready` = the post-claim reconnect succeeded and the Meet-your-lamp pane's
// Continue button should enable. `working` covers the background reconnect.
enum AddLampStatus { idle, working, ready, error }

enum AddLampError { none, wrongPassword, claimFailed, connectFailed }

@freezed
abstract class AddLampState with _$AddLampState {
  const factory AddLampState({
    @Default(AddLampStep.scan) AddLampStep step,
    @Default('') String deviceId,
    @Default('') String name,
    @Default('') String password,
    // Snapshotted from the nearby scan at select() — the lamp has left the
    // scan list by the Meet pane (it's rebooting), so carry its colours
    // through for the critter recolor.
    @Default(0) int baseRgb,
    @Default(0) int shadeRgb,
    @Default(AddLampStatus.idle) AddLampStatus status,
    @Default(AddLampError.none) AddLampError error,
    String? errorMessage,
  }) = _AddLampState;

  factory AddLampState.fromJson(Map<String, dynamic> json) =>
      _$AddLampStateFromJson(json);
}
