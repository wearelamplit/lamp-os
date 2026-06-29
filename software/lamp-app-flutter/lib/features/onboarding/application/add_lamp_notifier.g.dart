// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'add_lamp_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(AddLampNotifier)
final addLampNotifierProvider = AddLampNotifierProvider._();

final class AddLampNotifierProvider
    extends $NotifierProvider<AddLampNotifier, AddLampState> {
  AddLampNotifierProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'addLampNotifierProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$addLampNotifierHash();

  @$internal
  @override
  AddLampNotifier create() => AddLampNotifier();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(AddLampState value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<AddLampState>(value),
    );
  }
}

String _$addLampNotifierHash() => r'd02e692642210f9df16dc65a87a98390309f6e4b';

abstract class _$AddLampNotifier extends $Notifier<AddLampState> {
  AddLampState build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AddLampState, AddLampState>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AddLampState, AddLampState>,
              AddLampState,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
