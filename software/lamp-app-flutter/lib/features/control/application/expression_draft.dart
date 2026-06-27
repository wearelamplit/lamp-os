import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../lamp_shell/domain/expression_meta.dart';
import '../domain/lamp_color.dart';
import '../domain/sections.dart';
import 'control_notifier.dart';

part 'expression_draft.g.dart';

/// Default starting swatch for a brand-new expression — lime yellow
/// (`0xDDFF77`). Seeded so the user
/// always has at least one color to work with; they can edit or add
/// more from the editor.
const _defaultNewColor = LampColor(r: 0xDD, g: 0xFF, b: 0x77, w: 0);

/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target) — this is the path used for new entries after the user
/// picks them in `AddExpressionPickerScreen`.
@Riverpod(keepAlive: true, name: 'expressionDraftProvider')
class ExpressionDraft extends _$ExpressionDraft {
  @override
  ExpressionConfig build(String lampId, String type, int target) {
    final state = ref.read(controlNotifierProvider(lampId)).value;
    if (state != null) {
      final existing = state.expressions.expressions
          .where((e) => e.type == type && e.target == target);
      if (existing.isNotEmpty) return existing.first;
    }
    // No matching entry — build a default draft pre-populated with the
    // firmware's documented defaults for this type.
    final meta = ExpressionTypeMeta.byKey(type);
    return ExpressionConfig(
      type: type,
      enabled: true,
      colors: const [_defaultNewColor],
      intervalMin: 60,
      intervalMax: 900,
      target: target,
      parameters: Map<String, int>.from(
          meta?.defaultParameters ?? const <String, int>{}),
    );
  }

  /// Replace the draft with the result of [f] applied to the current value.
  void update(ExpressionConfig Function(ExpressionConfig current) f) =>
      state = f(state);

  /// Drop the draft so the next [build] gets a fresh copy. Call after Save
  /// or Delete to avoid leaking stale state into the next editor visit.
  void reset() => ref.invalidateSelf();
}
