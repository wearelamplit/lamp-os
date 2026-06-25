// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'wifi_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Owns the live `wifiOp` write surface and the `wifiState` notify stream
/// for a single lamp. Seeded by a read on build(); notifies thereafter.
///
/// Per-lamp via the `deviceId` family arg.

@ProviderFor(WifiNotifier)
final wifiNotifierProvider = WifiNotifierFamily._();

/// Owns the live `wifiOp` write surface and the `wifiState` notify stream
/// for a single lamp. Seeded by a read on build(); notifies thereafter.
///
/// Per-lamp via the `deviceId` family arg.
final class WifiNotifierProvider
    extends $AsyncNotifierProvider<WifiNotifier, WifiState> {
  /// Owns the live `wifiOp` write surface and the `wifiState` notify stream
  /// for a single lamp. Seeded by a read on build(); notifies thereafter.
  ///
  /// Per-lamp via the `deviceId` family arg.
  WifiNotifierProvider._({
    required WifiNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'wifiNotifierProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$wifiNotifierHash();

  @override
  String toString() {
    return r'wifiNotifierProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  WifiNotifier create() => WifiNotifier();

  @override
  bool operator ==(Object other) {
    return other is WifiNotifierProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$wifiNotifierHash() => r'28ca8267c8a373a0cbaef1f2bb43a72c456fc5fb';

/// Owns the live `wifiOp` write surface and the `wifiState` notify stream
/// for a single lamp. Seeded by a read on build(); notifies thereafter.
///
/// Per-lamp via the `deviceId` family arg.

final class WifiNotifierFamily extends $Family
    with
        $ClassFamilyOverride<
          WifiNotifier,
          AsyncValue<WifiState>,
          WifiState,
          FutureOr<WifiState>,
          String
        > {
  WifiNotifierFamily._()
    : super(
        retry: null,
        name: r'wifiNotifierProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Owns the live `wifiOp` write surface and the `wifiState` notify stream
  /// for a single lamp. Seeded by a read on build(); notifies thereafter.
  ///
  /// Per-lamp via the `deviceId` family arg.

  WifiNotifierProvider call(String deviceId) =>
      WifiNotifierProvider._(argument: deviceId, from: this);

  @override
  String toString() => r'wifiNotifierProvider';
}

/// Owns the live `wifiOp` write surface and the `wifiState` notify stream
/// for a single lamp. Seeded by a read on build(); notifies thereafter.
///
/// Per-lamp via the `deviceId` family arg.

abstract class _$WifiNotifier extends $AsyncNotifier<WifiState> {
  late final _$args = ref.$arg as String;
  String get deviceId => _$args;

  FutureOr<WifiState> build(String deviceId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AsyncValue<WifiState>, WifiState>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<WifiState>, WifiState>,
              AsyncValue<WifiState>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
