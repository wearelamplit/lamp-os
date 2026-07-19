// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'dispositions_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write: the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
/// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
/// peer's user-set name or the phone's BLE remoteId. The firmware emits the
/// same address on both platforms, so keys are stable across Android and iOS.

@ProviderFor(Dispositions)
final dispositionsProvider = DispositionsFamily._();

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write: the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
/// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
/// peer's user-set name or the phone's BLE remoteId. The firmware emits the
/// same address on both platforms, so keys are stable across Android and iOS.
final class DispositionsProvider
    extends $AsyncNotifierProvider<Dispositions, Map<String, int>> {
  /// Per-peer social disposition for a given lamp.
  ///
  /// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
  /// an in-memory map AND schedule a debounced write back to the lamp (500
  /// ms after the last edit). The full map is sent on every write: the
  /// firmware-side characteristic replaces the entire state on each write.
  ///
  /// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
  /// Missing keys default to 3 (neutral) at the call site via `get`.
  ///
  /// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
  /// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
  /// peer's user-set name or the phone's BLE remoteId. The firmware emits the
  /// same address on both platforms, so keys are stable across Android and iOS.
  DispositionsProvider._({
    required DispositionsFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'dispositionsProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$dispositionsHash();

  @override
  String toString() {
    return r'dispositionsProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  Dispositions create() => Dispositions();

  @override
  bool operator ==(Object other) {
    return other is DispositionsProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$dispositionsHash() => r'287cdb6e46f32c8d55d2d4f76c650203a8d8889e';

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write: the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
/// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
/// peer's user-set name or the phone's BLE remoteId. The firmware emits the
/// same address on both platforms, so keys are stable across Android and iOS.

final class DispositionsFamily extends $Family
    with
        $ClassFamilyOverride<
          Dispositions,
          AsyncValue<Map<String, int>>,
          Map<String, int>,
          FutureOr<Map<String, int>>,
          String
        > {
  DispositionsFamily._()
    : super(
        retry: null,
        name: r'dispositionsProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Per-peer social disposition for a given lamp.
  ///
  /// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
  /// an in-memory map AND schedule a debounced write back to the lamp (500
  /// ms after the last edit). The full map is sent on every write: the
  /// firmware-side characteristic replaces the entire state on each write.
  ///
  /// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
  /// Missing keys default to 3 (neutral) at the call site via `get`.
  ///
  /// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
  /// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
  /// peer's user-set name or the phone's BLE remoteId. The firmware emits the
  /// same address on both platforms, so keys are stable across Android and iOS.

  DispositionsProvider call(String lampId) =>
      DispositionsProvider._(argument: lampId, from: this);

  @override
  String toString() => r'dispositionsProvider';
}

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write: the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
/// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
/// peer's user-set name or the phone's BLE remoteId. The firmware emits the
/// same address on both platforms, so keys are stable across Android and iOS.

abstract class _$Dispositions extends $AsyncNotifier<Map<String, int>> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  FutureOr<Map<String, int>> build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref =
        this.ref as $Ref<AsyncValue<Map<String, int>>, Map<String, int>>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<Map<String, int>>, Map<String, int>>,
              AsyncValue<Map<String, int>>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
