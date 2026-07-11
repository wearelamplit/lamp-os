// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'inventory_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(InventoryNotifier)
final inventoryNotifierProvider = InventoryNotifierProvider._();

final class InventoryNotifierProvider
    extends $AsyncNotifierProvider<InventoryNotifier, List<InventoryLamp>> {
  InventoryNotifierProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'inventoryNotifierProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$inventoryNotifierHash();

  @$internal
  @override
  InventoryNotifier create() => InventoryNotifier();
}

String _$inventoryNotifierHash() => r'c6a030c4a3e1381f9f854834326c918f00fc6671';

abstract class _$InventoryNotifier extends $AsyncNotifier<List<InventoryLamp>> {
  FutureOr<List<InventoryLamp>> build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref =
        this.ref as $Ref<AsyncValue<List<InventoryLamp>>, List<InventoryLamp>>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<List<InventoryLamp>>, List<InventoryLamp>>,
              AsyncValue<List<InventoryLamp>>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
