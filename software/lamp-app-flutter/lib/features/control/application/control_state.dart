import 'package:freezed_annotation/freezed_annotation.dart';

import '../../lamp_shell/domain/expression_catalog.dart';
import '../domain/sections.dart';

part 'control_state.freezed.dart';

/// Active greeting reported by CHAR_STATE_NOTIFY. Null when inactive or when
/// older firmware sends no `greeting` key.
class GreetingState {
  const GreetingState({required this.peer, required this.kind});
  final String peer; // uppercase colon-hex mesh MAC (lampId)
  final String kind; // e.g. "glitch", "warm", "reserved", "snub"

  @override
  bool operator ==(Object other) =>
      other is GreetingState && other.peer == peer && other.kind == kind;

  @override
  int get hashCode => Object.hash(peer, kind);
}

/// Combined state for the Control screen. Populated by ControlNotifier after
/// connect+auth+per-section reads. Not JSON-serialized, purely in-memory.
///
/// `connected` and `reconnectAttempt` track the BLE link state.
@freezed
abstract class ControlState with _$ControlState {
  const factory ControlState({
    required LampSection lamp,
    required BaseSection base,
    required ShadeSection shade,
    required HomeSection home,
    required ExpressionsSection expressions,
    // Firmware-declared expression catalog (exprcat page-section). Null on
    // older firmware that predates it; the editor degrades to colors-only.
    ExpressionCatalog? catalog,
    @Default(true) bool connected,
    @Default(0) int reconnectAttempt,
    // Firmware-truth bit for the expression editor's Test button. True
    // while the lamp is playing back an expression triggered via
    // dispatchLampAction("test_expression"); the firmware flips it back
    // to false the instant the entry's animationState returns to STOPPED
    // (one-shot expressions) or on test_expression_complete (continuous
    // expressions). Source: CHAR_STATE_NOTIFY `previewActive` field.
    @Default(false) bool previewActive,
    // Active greeting. Non-null while the lamp is greeting a peer.
    // Source: CHAR_STATE_NOTIFY `greeting` object. Null on older firmware.
    GreetingState? greeting,
  }) = _ControlState;
}
