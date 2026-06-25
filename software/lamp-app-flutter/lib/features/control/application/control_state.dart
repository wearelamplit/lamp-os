import 'package:freezed_annotation/freezed_annotation.dart';

import '../domain/sections.dart';

part 'control_state.freezed.dart';

/// Combined state for the Control screen. Populated by ControlNotifier after
/// connect+auth+per-section reads. Not JSON-serialized — purely in-memory.
///
/// `connected` and `reconnectAttempt` track the BLE link state. They do NOT
/// participate in `isDirty` comparisons (see ControlNotifier._is*Dirty
/// helpers) — a link drop alone shouldn't enable the Save button.
@freezed
abstract class ControlState with _$ControlState {
  const factory ControlState({
    required LampSection lamp,
    required BaseSection base,
    required ShadeSection shade,
    required HomeSection home,
    required ExpressionsSection expressions,
    @Default(true) bool connected,
    @Default(0) int reconnectAttempt,
    // Firmware-truth bit for the expression editor's Test button. True
    // while the lamp is playing back an expression triggered via
    // dispatchLampAction("test_expression"); the firmware flips it back
    // to false the instant the entry's animationState returns to STOPPED
    // (one-shot expressions) or on test_expression_complete (continuous
    // expressions). Source: CHAR_STATE_NOTIFY `previewActive` field.
    @Default(false) bool previewActive,
  }) = _ControlState;
}
