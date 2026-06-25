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
