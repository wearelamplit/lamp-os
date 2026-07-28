import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';

part 'manual_palette_draft.g.dart';

/// In-flight manual-palette editor draft for one wisp, keyed by lampId.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor.
@Riverpod(keepAlive: true, name: 'manualPaletteDraftProvider')
class ManualPaletteDraft extends _$ManualPaletteDraft {
  @override
  List<LampColor> build(String lampId) => const <LampColor>[];

  /// Emit a fresh list so watchers rebuild on identity change.
  void set(List<LampColor> colors) =>
      state = List<LampColor>.unmodifiable(colors);
}
