// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'active_lamp_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(ActiveLampNotifier)
final activeLampNotifierProvider = ActiveLampNotifierProvider._();

final class ActiveLampNotifierProvider
    extends $AsyncNotifierProvider<ActiveLampNotifier, String?> {
  ActiveLampNotifierProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'activeLampNotifierProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$activeLampNotifierHash();

  @$internal
  @override
  ActiveLampNotifier create() => ActiveLampNotifier();
}

String _$activeLampNotifierHash() =>
    r'b94da793387f097f13e7e1802a61263da5f153ae';

abstract class _$ActiveLampNotifier extends $AsyncNotifier<String?> {
  FutureOr<String?> build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AsyncValue<String?>, String?>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<String?>, String?>,
              AsyncValue<String?>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
