import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/domain/lamp_color.dart';

part 'manual_palette_draft.g.dart';

/// The editor draft and the saved palette it is measured against: the draft
/// is an unsaved edit exactly when it differs from [saved].
typedef ManualPaletteDraftState = ({
  List<LampColor> colors,
  List<LampColor> saved,
});

/// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
/// the last saved palette known for it.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor. The saved
/// palette lives beside the draft so a WispNotifier built on a later visit
/// to the lamp still tells an untouched draft from an edit.
@Riverpod(keepAlive: true, name: 'manualPaletteDraftProvider')
class ManualPaletteDraft extends _$ManualPaletteDraft {
  @override
  ManualPaletteDraftState build(String lampId) =>
      (colors: const <LampColor>[], saved: const <LampColor>[]);

  /// Emit a fresh list so watchers rebuild on identity change.
  void set(List<LampColor> colors) => state =
      (colors: List<LampColor>.unmodifiable(colors), saved: state.saved);

  void setSaved(List<LampColor> saved) => state =
      (colors: state.colors, saved: List<LampColor>.unmodifiable(saved));
}
