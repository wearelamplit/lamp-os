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
/// ms after the last edit). The full map is sent on every write — the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are BD_ADDR strings (canonical uppercase colon-hex,
/// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
/// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
/// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
/// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
/// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
/// emits `bdAddr` per peer; the social screen currently uses the
/// phone-direct scan).

@ProviderFor(Dispositions)
final dispositionsProvider = DispositionsFamily._();

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write — the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are BD_ADDR strings (canonical uppercase colon-hex,
/// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
/// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
/// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
/// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
/// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
/// emits `bdAddr` per peer; the social screen currently uses the
/// phone-direct scan).
final class DispositionsProvider
    extends $AsyncNotifierProvider<Dispositions, Map<String, int>> {
  /// Per-peer social disposition for a given lamp.
  ///
  /// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
  /// an in-memory map AND schedule a debounced write back to the lamp (500
  /// ms after the last edit). The full map is sent on every write — the
  /// firmware-side characteristic replaces the entire state on each write.
  ///
  /// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
  /// Missing keys default to 3 (neutral) at the call site via `get`.
  ///
  /// Keys are BD_ADDR strings (canonical uppercase colon-hex,
  /// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
  /// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
  /// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
  /// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
  /// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
  /// emits `bdAddr` per peer; the social screen currently uses the
  /// phone-direct scan).
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

String _$dispositionsHash() => r'67fc5d630c14a7267f1b3e268620c895fc3dbee1';

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write — the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are BD_ADDR strings (canonical uppercase colon-hex,
/// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
/// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
/// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
/// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
/// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
/// emits `bdAddr` per peer; the social screen currently uses the
/// phone-direct scan).

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
  /// ms after the last edit). The full map is sent on every write — the
  /// firmware-side characteristic replaces the entire state on each write.
  ///
  /// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
  /// Missing keys default to 3 (neutral) at the call site via `get`.
  ///
  /// Keys are BD_ADDR strings (canonical uppercase colon-hex,
  /// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
  /// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
  /// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
  /// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
  /// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
  /// emits `bdAddr` per peer; the social screen currently uses the
  /// phone-direct scan).

  DispositionsProvider call(String lampId) =>
      DispositionsProvider._(argument: lampId, from: this);

  @override
  String toString() => r'dispositionsProvider';
}

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write — the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are BD_ADDR strings (canonical uppercase colon-hex,
/// e.g. "AA:BB:CC:DD:EE:FF") — NOT the peer's user-set name. On
/// Android, `NearbyLamp.id` is the BLE remoteId which IS the BD_ADDR
/// in this exact format. On iOS the BLE remoteId is a CoreBluetooth
/// UUID, NOT a BD_ADDR — iOS cross-reference is a known follow-up
/// (would require reading the lamp's CHAR_NEARBY_LAMPS JSON which
/// emits `bdAddr` per peer; the social screen currently uses the
/// phone-direct scan).

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
