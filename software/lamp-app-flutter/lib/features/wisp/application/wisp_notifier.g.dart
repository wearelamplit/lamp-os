// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'wisp_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Owns the live [WispStatus] for a single lamp. On build it does one
/// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
/// lamp without it) and subscribes to the status notify stream; thereafter
/// every wispStatus update from the wisp lands in `state` without a
/// round-trip. The wisp's settings come from the `wispconfig` section when
/// the lamp serves it, re-read whenever the status's `configLastSeenMs`
/// moves, and are merged over whatever the status carried.
///
/// `setOscRole` delegates to the repository and relies on the wisp's
/// on-change broadcast (≤ ~2s) to push the updated status back via
/// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
/// the picker doesn't lag the tap; the notify either confirms or corrects it.

@ProviderFor(WispNotifier)
final wispNotifierProvider = WispNotifierFamily._();

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
/// lamp without it) and subscribes to the status notify stream; thereafter
/// every wispStatus update from the wisp lands in `state` without a
/// round-trip. The wisp's settings come from the `wispconfig` section when
/// the lamp serves it, re-read whenever the status's `configLastSeenMs`
/// moves, and are merged over whatever the status carried.
///
/// `setOscRole` delegates to the repository and relies on the wisp's
/// on-change broadcast (≤ ~2s) to push the updated status back via
/// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
/// the picker doesn't lag the tap; the notify either confirms or corrects it.
final class WispNotifierProvider
    extends $AsyncNotifierProvider<WispNotifier, WispStatus> {
  /// Owns the live [WispStatus] for a single lamp. On build it does one
  /// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
  /// lamp without it) and subscribes to the status notify stream; thereafter
  /// every wispStatus update from the wisp lands in `state` without a
  /// round-trip. The wisp's settings come from the `wispconfig` section when
  /// the lamp serves it, re-read whenever the status's `configLastSeenMs`
  /// moves, and are merged over whatever the status carried.
  ///
  /// `setOscRole` delegates to the repository and relies on the wisp's
  /// on-change broadcast (≤ ~2s) to push the updated status back via
  /// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
  /// the picker doesn't lag the tap; the notify either confirms or corrects it.
  WispNotifierProvider._({
    required WispNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'wispNotifierProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$wispNotifierHash();

  @override
  String toString() {
    return r'wispNotifierProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  WispNotifier create() => WispNotifier();

  @override
  bool operator ==(Object other) {
    return other is WispNotifierProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$wispNotifierHash() => r'c6f7f7f57cf830d350d540931ad2d691dfacc12e';

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
/// lamp without it) and subscribes to the status notify stream; thereafter
/// every wispStatus update from the wisp lands in `state` without a
/// round-trip. The wisp's settings come from the `wispconfig` section when
/// the lamp serves it, re-read whenever the status's `configLastSeenMs`
/// moves, and are merged over whatever the status carried.
///
/// `setOscRole` delegates to the repository and relies on the wisp's
/// on-change broadcast (≤ ~2s) to push the updated status back via
/// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
/// the picker doesn't lag the tap; the notify either confirms or corrects it.

final class WispNotifierFamily extends $Family
    with
        $ClassFamilyOverride<
          WispNotifier,
          AsyncValue<WispStatus>,
          WispStatus,
          FutureOr<WispStatus>,
          String
        > {
  WispNotifierFamily._()
    : super(
        retry: null,
        name: r'wispNotifierProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Owns the live [WispStatus] for a single lamp. On build it does one
  /// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
  /// lamp without it) and subscribes to the status notify stream; thereafter
  /// every wispStatus update from the wisp lands in `state` without a
  /// round-trip. The wisp's settings come from the `wispconfig` section when
  /// the lamp serves it, re-read whenever the status's `configLastSeenMs`
  /// moves, and are merged over whatever the status carried.
  ///
  /// `setOscRole` delegates to the repository and relies on the wisp's
  /// on-change broadcast (≤ ~2s) to push the updated status back via
  /// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
  /// the picker doesn't lag the tap; the notify either confirms or corrects it.

  WispNotifierProvider call(String lampId) =>
      WispNotifierProvider._(argument: lampId, from: this);

  @override
  String toString() => r'wispNotifierProvider';
}

/// Owns the live [WispStatus] for a single lamp. On build it does one
/// status read (the `wispstatus` page section, or `CHAR_WISP_STATUS` on a
/// lamp without it) and subscribes to the status notify stream; thereafter
/// every wispStatus update from the wisp lands in `state` without a
/// round-trip. The wisp's settings come from the `wispconfig` section when
/// the lamp serves it, re-read whenever the status's `configLastSeenMs`
/// moves, and are merged over whatever the status carried.
///
/// `setOscRole` delegates to the repository and relies on the wisp's
/// on-change broadcast (≤ ~2s) to push the updated status back via
/// CHAR_WISP_STATUS. Optimistically reflects the choice in local state so
/// the picker doesn't lag the tap; the notify either confirms or corrects it.

abstract class _$WispNotifier extends $AsyncNotifier<WispStatus> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  FutureOr<WispStatus> build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AsyncValue<WispStatus>, WispStatus>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<WispStatus>, WispStatus>,
              AsyncValue<WispStatus>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
