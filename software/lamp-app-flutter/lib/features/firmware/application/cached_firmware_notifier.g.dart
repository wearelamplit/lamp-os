// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'cached_firmware_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(CachedFirmwareNotifier)
final cachedFirmwareNotifierProvider = CachedFirmwareNotifierProvider._();

final class CachedFirmwareNotifierProvider
    extends
        $AsyncNotifierProvider<
          CachedFirmwareNotifier,
          Map<String, CachedFirmware>
        > {
  CachedFirmwareNotifierProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'cachedFirmwareNotifierProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$cachedFirmwareNotifierHash();

  @$internal
  @override
  CachedFirmwareNotifier create() => CachedFirmwareNotifier();
}

String _$cachedFirmwareNotifierHash() =>
    r'4f5eee407299963ad436c6c7652dc8dfc348767b';

abstract class _$CachedFirmwareNotifier
    extends $AsyncNotifier<Map<String, CachedFirmware>> {
  FutureOr<Map<String, CachedFirmware>> build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref =
        this.ref
            as $Ref<
              AsyncValue<Map<String, CachedFirmware>>,
              Map<String, CachedFirmware>
            >;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<
                AsyncValue<Map<String, CachedFirmware>>,
                Map<String, CachedFirmware>
              >,
              AsyncValue<Map<String, CachedFirmware>>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
