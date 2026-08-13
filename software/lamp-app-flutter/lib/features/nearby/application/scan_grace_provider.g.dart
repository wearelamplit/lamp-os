// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'scan_grace_provider.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// `true` for [_scanGrace] from the moment something first watches this
/// provider, then `false` for as long as a watcher stays mounted.
///
/// Auto-dispose so the Timer is torn down together with the widgets
/// that care about it — without this, widget tests (which sync-dispose
/// the container after the test body returns) would trip on
/// `!timersPending`. The trade-off is that if every watcher unmounts
/// and a new one mounts later, the grace re-arms — that mirrors the
/// user-facing intent: "we just opened a list, give a beat to hear
/// in-range lamps before flagging them offline."

@ProviderFor(ScanGraceActive)
final scanGraceActiveProvider = ScanGraceActiveProvider._();

/// `true` for [_scanGrace] from the moment something first watches this
/// provider, then `false` for as long as a watcher stays mounted.
///
/// Auto-dispose so the Timer is torn down together with the widgets
/// that care about it — without this, widget tests (which sync-dispose
/// the container after the test body returns) would trip on
/// `!timersPending`. The trade-off is that if every watcher unmounts
/// and a new one mounts later, the grace re-arms — that mirrors the
/// user-facing intent: "we just opened a list, give a beat to hear
/// in-range lamps before flagging them offline."
final class ScanGraceActiveProvider
    extends $NotifierProvider<ScanGraceActive, bool> {
  /// `true` for [_scanGrace] from the moment something first watches this
  /// provider, then `false` for as long as a watcher stays mounted.
  ///
  /// Auto-dispose so the Timer is torn down together with the widgets
  /// that care about it — without this, widget tests (which sync-dispose
  /// the container after the test body returns) would trip on
  /// `!timersPending`. The trade-off is that if every watcher unmounts
  /// and a new one mounts later, the grace re-arms — that mirrors the
  /// user-facing intent: "we just opened a list, give a beat to hear
  /// in-range lamps before flagging them offline."
  ScanGraceActiveProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'scanGraceActiveProvider',
        isAutoDispose: true,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$scanGraceActiveHash();

  @$internal
  @override
  ScanGraceActive create() => ScanGraceActive();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(bool value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<bool>(value),
    );
  }
}

String _$scanGraceActiveHash() => r'f9a29e1a45a1e86abff53cbed6b6bf4b5cb4df8b';

/// `true` for [_scanGrace] from the moment something first watches this
/// provider, then `false` for as long as a watcher stays mounted.
///
/// Auto-dispose so the Timer is torn down together with the widgets
/// that care about it — without this, widget tests (which sync-dispose
/// the container after the test body returns) would trip on
/// `!timersPending`. The trade-off is that if every watcher unmounts
/// and a new one mounts later, the grace re-arms — that mirrors the
/// user-facing intent: "we just opened a list, give a beat to hear
/// in-range lamps before flagging them offline."

abstract class _$ScanGraceActive extends $Notifier<bool> {
  bool build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<bool, bool>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<bool, bool>,
              bool,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
