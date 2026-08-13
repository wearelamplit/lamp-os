// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'firmware_release_client_provider.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(firmwareReleaseClient)
final firmwareReleaseClientProvider = FirmwareReleaseClientProvider._();

final class FirmwareReleaseClientProvider
    extends
        $FunctionalProvider<
          FirmwareReleaseClient,
          FirmwareReleaseClient,
          FirmwareReleaseClient
        >
    with $Provider<FirmwareReleaseClient> {
  FirmwareReleaseClientProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'firmwareReleaseClientProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$firmwareReleaseClientHash();

  @$internal
  @override
  $ProviderElement<FirmwareReleaseClient> $createElement(
    $ProviderPointer pointer,
  ) => $ProviderElement(pointer);

  @override
  FirmwareReleaseClient create(Ref ref) {
    return firmwareReleaseClient(ref);
  }

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(FirmwareReleaseClient value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<FirmwareReleaseClient>(value),
    );
  }
}

String _$firmwareReleaseClientHash() =>
    r'26a4b339c6058f0e58bcdc62751b6b0cc31c4e4e';
