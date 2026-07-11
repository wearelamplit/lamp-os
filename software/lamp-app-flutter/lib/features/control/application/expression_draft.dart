import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../lamp_shell/domain/expression_catalog.dart';
import '../domain/lamp_color.dart';
import '../domain/sections.dart';
import 'control_notifier.dart';
import 'control_state.dart';

part 'expression_draft.g.dart';

/// Default starting swatch for a brand-new expression — lime yellow
/// (`0xDDFF77`). Seeded so the user always has at least one color to work
/// with; they can edit or add more from the editor.
const _defaultNewColor = LampColor(r: 0xDD, g: 0xFF, b: 0x77, w: 0);

/// Working copy of an expression while the user is editing it. Held in a
/// keep-alive provider so navigating away from the editor and back doesn't
/// lose in-progress edits. Save / Delete invalidate the entry.
///
/// Family key is `(lampId, type, target)`. If the family entry doesn't
/// match any existing expression, a fresh draft is created with that
/// (type, target), seeded from the firmware catalog's declared defaults.
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
    final descriptor = state?.catalog?.byId(type);
    final pixelCount = _pixelCountFor(state, target);
    return ExpressionConfig(
      type: type,
      enabled: true,
      colors: descriptor?.colors.inheritsSurface == true
          ? const []
          : const [_defaultNewColor],
      intervalMin: descriptor?.interval?.defLo ?? 60,
      intervalMax: descriptor?.interval?.defHi ?? 900,
      target: target,
      parameters: _seedParams(descriptor, pixelCount),
    );
  }

  static int _pixelCountFor(ControlState? state, int target) {
    if (state == null) return 0;
    final shade = state.shade.px;
    final base = state.base.px;
    return switch (target) {
      1 => shade,
      2 => base,
      _ => shade > base ? shade : base,
    };
  }

  static Map<String, int> _seedParams(
      ExpressionDescriptor? descriptor, int pixelCount) {
    final params = <String, int>{};
    if (descriptor == null) return params;
    for (final p in descriptor.params) {
      params[p.key] = p.def.resolve(pixelCount);
    }
    final duration = descriptor.duration;
    if (duration != null &&
        duration.minKey != null &&
        duration.maxKey != null) {
      params[duration.minKey!] = duration.defLo;
      params[duration.maxKey!] = duration.defHi;
    }
    return params;
  }

  /// Replace the draft with the result of [f] applied to the current value.
  void update(ExpressionConfig Function(ExpressionConfig current) f) =>
      state = f(state);

  /// Drop the draft so the next [build] gets a fresh copy. Call after Save
  /// or Delete to avoid leaking stale state into the next editor visit.
  void reset() => ref.invalidateSelf();
}
