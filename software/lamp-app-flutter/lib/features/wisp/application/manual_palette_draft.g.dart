// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'manual_palette_draft.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
/// the last saved palette known for it.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor. The saved
/// palette lives beside the draft so a WispNotifier built on a later visit
/// to the lamp can tell an untouched draft, which it clears, from an edit,
/// which it keeps.

@ProviderFor(ManualPaletteDraft)
final manualPaletteDraftProvider = ManualPaletteDraftFamily._();

/// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
/// the last saved palette known for it.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor. The saved
/// palette lives beside the draft so a WispNotifier built on a later visit
/// to the lamp can tell an untouched draft, which it clears, from an edit,
/// which it keeps.
final class ManualPaletteDraftProvider
    extends $NotifierProvider<ManualPaletteDraft, ManualPaletteDraftState> {
  /// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
  /// the last saved palette known for it.
  ///
  /// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
  /// gradient bar on its own: WispStatus equality doesn't cover the draft, so
  /// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
  /// WispNotifier writes on read survives until a widget watches it, and
  /// in-progress edits outlast a navigation away from the editor. The saved
  /// palette lives beside the draft so a WispNotifier built on a later visit
  /// to the lamp can tell an untouched draft, which it clears, from an edit,
  /// which it keeps.
  ManualPaletteDraftProvider._({
    required ManualPaletteDraftFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'manualPaletteDraftProvider',
         isAutoDispose: false,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$manualPaletteDraftHash();

  @override
  String toString() {
    return r'manualPaletteDraftProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  ManualPaletteDraft create() => ManualPaletteDraft();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(ManualPaletteDraftState value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<ManualPaletteDraftState>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is ManualPaletteDraftProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$manualPaletteDraftHash() =>
    r'8e814ab1f54ed2dd4fe2a1f79e6c0230a60845c8';

/// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
/// the last saved palette known for it.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor. The saved
/// palette lives beside the draft so a WispNotifier built on a later visit
/// to the lamp can tell an untouched draft, which it clears, from an edit,
/// which it keeps.

final class ManualPaletteDraftFamily extends $Family
    with
        $ClassFamilyOverride<
          ManualPaletteDraft,
          ManualPaletteDraftState,
          ManualPaletteDraftState,
          ManualPaletteDraftState,
          String
        > {
  ManualPaletteDraftFamily._()
    : super(
        retry: null,
        name: r'manualPaletteDraftProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: false,
      );

  /// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
  /// the last saved palette known for it.
  ///
  /// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
  /// gradient bar on its own: WispStatus equality doesn't cover the draft, so
  /// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
  /// WispNotifier writes on read survives until a widget watches it, and
  /// in-progress edits outlast a navigation away from the editor. The saved
  /// palette lives beside the draft so a WispNotifier built on a later visit
  /// to the lamp can tell an untouched draft, which it clears, from an edit,
  /// which it keeps.

  ManualPaletteDraftProvider call(String lampId) =>
      ManualPaletteDraftProvider._(argument: lampId, from: this);

  @override
  String toString() => r'manualPaletteDraftProvider';
}

/// In-flight manual-palette editor draft for one wisp, keyed by lampId, with
/// the last saved palette known for it.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor. The saved
/// palette lives beside the draft so a WispNotifier built on a later visit
/// to the lamp can tell an untouched draft, which it clears, from an edit,
/// which it keeps.

abstract class _$ManualPaletteDraft extends $Notifier<ManualPaletteDraftState> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  ManualPaletteDraftState build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref =
        this.ref as $Ref<ManualPaletteDraftState, ManualPaletteDraftState>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<ManualPaletteDraftState, ManualPaletteDraftState>,
              ManualPaletteDraftState,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
