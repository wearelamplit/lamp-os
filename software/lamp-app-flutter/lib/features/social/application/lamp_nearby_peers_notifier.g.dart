// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'lamp_nearby_peers_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Per-lamp view of peers the connected lamp can hear. Reads the
/// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects — only surfaces AsyncError after sustained
/// failure (currently: never auto-fails; the social screen will
/// render "No lamps nearby" once the connection is restored and a
/// real empty response lands).
///
/// Family-keyed by lampId (deviceId in the BleClient sense). Each
/// lamp connection runs its own polling loop, scoped to the screen
/// that's looking at that lamp.

@ProviderFor(LampNearbyPeersNotifier)
final lampNearbyPeersNotifierProvider = LampNearbyPeersNotifierFamily._();

/// Per-lamp view of peers the connected lamp can hear. Reads the
/// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects — only surfaces AsyncError after sustained
/// failure (currently: never auto-fails; the social screen will
/// render "No lamps nearby" once the connection is restored and a
/// real empty response lands).
///
/// Family-keyed by lampId (deviceId in the BleClient sense). Each
/// lamp connection runs its own polling loop, scoped to the screen
/// that's looking at that lamp.
final class LampNearbyPeersNotifierProvider
    extends
        $AsyncNotifierProvider<LampNearbyPeersNotifier, List<LampNearbyPeer>> {
  /// Per-lamp view of peers the connected lamp can hear. Reads the
  /// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
  ///
  /// Empty list while loading. Keeps the last-good snapshot on parse
  /// errors and disconnects — only surfaces AsyncError after sustained
  /// failure (currently: never auto-fails; the social screen will
  /// render "No lamps nearby" once the connection is restored and a
  /// real empty response lands).
  ///
  /// Family-keyed by lampId (deviceId in the BleClient sense). Each
  /// lamp connection runs its own polling loop, scoped to the screen
  /// that's looking at that lamp.
  LampNearbyPeersNotifierProvider._({
    required LampNearbyPeersNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'lampNearbyPeersNotifierProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$lampNearbyPeersNotifierHash();

  @override
  String toString() {
    return r'lampNearbyPeersNotifierProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  LampNearbyPeersNotifier create() => LampNearbyPeersNotifier();

  @override
  bool operator ==(Object other) {
    return other is LampNearbyPeersNotifierProvider &&
        other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$lampNearbyPeersNotifierHash() =>
    r'974f170c3d2db79535b5bbd0e7e5b7f8be4e2565';

/// Per-lamp view of peers the connected lamp can hear. Reads the
/// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects — only surfaces AsyncError after sustained
/// failure (currently: never auto-fails; the social screen will
/// render "No lamps nearby" once the connection is restored and a
/// real empty response lands).
///
/// Family-keyed by lampId (deviceId in the BleClient sense). Each
/// lamp connection runs its own polling loop, scoped to the screen
/// that's looking at that lamp.

final class LampNearbyPeersNotifierFamily extends $Family
    with
        $ClassFamilyOverride<
          LampNearbyPeersNotifier,
          AsyncValue<List<LampNearbyPeer>>,
          List<LampNearbyPeer>,
          FutureOr<List<LampNearbyPeer>>,
          String
        > {
  LampNearbyPeersNotifierFamily._()
    : super(
        retry: null,
        name: r'lampNearbyPeersNotifierProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Per-lamp view of peers the connected lamp can hear. Reads the
  /// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
  ///
  /// Empty list while loading. Keeps the last-good snapshot on parse
  /// errors and disconnects — only surfaces AsyncError after sustained
  /// failure (currently: never auto-fails; the social screen will
  /// render "No lamps nearby" once the connection is restored and a
  /// real empty response lands).
  ///
  /// Family-keyed by lampId (deviceId in the BleClient sense). Each
  /// lamp connection runs its own polling loop, scoped to the screen
  /// that's looking at that lamp.

  LampNearbyPeersNotifierProvider call(String lampId) =>
      LampNearbyPeersNotifierProvider._(argument: lampId, from: this);

  @override
  String toString() => r'lampNearbyPeersNotifierProvider';
}

/// Per-lamp view of peers the connected lamp can hear. Reads the
/// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects — only surfaces AsyncError after sustained
/// failure (currently: never auto-fails; the social screen will
/// render "No lamps nearby" once the connection is restored and a
/// real empty response lands).
///
/// Family-keyed by lampId (deviceId in the BleClient sense). Each
/// lamp connection runs its own polling loop, scoped to the screen
/// that's looking at that lamp.

abstract class _$LampNearbyPeersNotifier
    extends $AsyncNotifier<List<LampNearbyPeer>> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  FutureOr<List<LampNearbyPeer>> build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref =
        this.ref
            as $Ref<AsyncValue<List<LampNearbyPeer>>, List<LampNearbyPeer>>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<
                AsyncValue<List<LampNearbyPeer>>,
                List<LampNearbyPeer>
              >,
              AsyncValue<List<LampNearbyPeer>>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
