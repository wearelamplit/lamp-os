// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'ble_client_provider.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(bleClient)
final bleClientProvider = BleClientProvider._();

final class BleClientProvider
    extends $FunctionalProvider<BleClient, BleClient, BleClient>
    with $Provider<BleClient> {
  BleClientProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'bleClientProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$bleClientHash();

  @$internal
  @override
  $ProviderElement<BleClient> $createElement($ProviderPointer pointer) =>
      $ProviderElement(pointer);

  @override
  BleClient create(Ref ref) {
    return bleClient(ref);
  }

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(BleClient value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<BleClient>(value),
    );
  }
}

String _$bleClientHash() => r'139c68287f3aea793ecc5132bd5a96f0e300a26b';

/// Live connection state for one lamp. Emits the current value on listen, then
/// on every link edge (including an unsolicited supervision-timeout drop), so a
/// UI watching it repaints the moment the lamp powers off.

@ProviderFor(lampConnected)
final lampConnectedProvider = LampConnectedFamily._();

/// Live connection state for one lamp. Emits the current value on listen, then
/// on every link edge (including an unsolicited supervision-timeout drop), so a
/// UI watching it repaints the moment the lamp powers off.

final class LampConnectedProvider
    extends $FunctionalProvider<AsyncValue<bool>, bool, Stream<bool>>
    with $FutureModifier<bool>, $StreamProvider<bool> {
  /// Live connection state for one lamp. Emits the current value on listen, then
  /// on every link edge (including an unsolicited supervision-timeout drop), so a
  /// UI watching it repaints the moment the lamp powers off.
  LampConnectedProvider._({
    required LampConnectedFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'lampConnectedProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$lampConnectedHash();

  @override
  String toString() {
    return r'lampConnectedProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  $StreamProviderElement<bool> $createElement($ProviderPointer pointer) =>
      $StreamProviderElement(pointer);

  @override
  Stream<bool> create(Ref ref) {
    final argument = this.argument as String;
    return lampConnected(ref, argument);
  }

  @override
  bool operator ==(Object other) {
    return other is LampConnectedProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$lampConnectedHash() => r'f3ec4afb54c99f25aa5cde073b624e7983357b02';

/// Live connection state for one lamp. Emits the current value on listen, then
/// on every link edge (including an unsolicited supervision-timeout drop), so a
/// UI watching it repaints the moment the lamp powers off.

final class LampConnectedFamily extends $Family
    with $FunctionalFamilyOverride<Stream<bool>, String> {
  LampConnectedFamily._()
    : super(
        retry: null,
        name: r'lampConnectedProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Live connection state for one lamp. Emits the current value on listen, then
  /// on every link edge (including an unsolicited supervision-timeout drop), so a
  /// UI watching it repaints the moment the lamp powers off.

  LampConnectedProvider call(String deviceId) =>
      LampConnectedProvider._(argument: deviceId, from: this);

  @override
  String toString() => r'lampConnectedProvider';
}
