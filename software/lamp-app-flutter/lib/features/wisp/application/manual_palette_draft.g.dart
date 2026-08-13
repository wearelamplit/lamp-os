// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'manual_palette_draft.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// In-flight manual-palette editor draft for one wisp, keyed by lampId.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor.

@ProviderFor(ManualPaletteDraft)
final manualPaletteDraftProvider = ManualPaletteDraftFamily._();

/// In-flight manual-palette editor draft for one wisp, keyed by lampId.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor.
final class ManualPaletteDraftProvider
    extends $NotifierProvider<ManualPaletteDraft, List<LampColor>> {
  /// In-flight manual-palette editor draft for one wisp, keyed by lampId.
  ///
  /// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
  /// gradient bar on its own: WispStatus equality doesn't cover the draft, so
  /// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
  /// WispNotifier writes on read survives until a widget watches it, and
  /// in-progress edits outlast a navigation away from the editor.
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
  Override overrideWithValue(List<LampColor> value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<List<LampColor>>(value),
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
    r'35bd7a347ac8af2158c19bc549ce6de07c871460';

/// In-flight manual-palette editor draft for one wisp, keyed by lampId.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor.

final class ManualPaletteDraftFamily extends $Family
    with
        $ClassFamilyOverride<
          ManualPaletteDraft,
          List<LampColor>,
          List<LampColor>,
          List<LampColor>,
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

  /// In-flight manual-palette editor draft for one wisp, keyed by lampId.
  ///
  /// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
  /// gradient bar on its own: WispStatus equality doesn't cover the draft, so
  /// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
  /// WispNotifier writes on read survives until a widget watches it, and
  /// in-progress edits outlast a navigation away from the editor.

  ManualPaletteDraftProvider call(String lampId) =>
      ManualPaletteDraftProvider._(argument: lampId, from: this);

  @override
  String toString() => r'manualPaletteDraftProvider';
}

/// In-flight manual-palette editor draft for one wisp, keyed by lampId.
///
/// Lives apart from WispNotifier so a swatch edit rebuilds the editor and
/// gradient bar on its own: WispStatus equality doesn't cover the draft, so
/// folding it into wispStatus dedups the rebuild away. keepAlive so the seed
/// WispNotifier writes on read survives until a widget watches it, and
/// in-progress edits outlast a navigation away from the editor.

abstract class _$ManualPaletteDraft extends $Notifier<List<LampColor>> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  List<LampColor> build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<List<LampColor>, List<LampColor>>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<List<LampColor>, List<LampColor>>,
              List<LampColor>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
