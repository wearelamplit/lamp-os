// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'dev_mode.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(DevMode)
final devModeProvider = DevModeProvider._();

final class DevModeProvider extends $AsyncNotifierProvider<DevMode, bool> {
  DevModeProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'devModeProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$devModeHash();

  @$internal
  @override
  DevMode create() => DevMode();
}

String _$devModeHash() => r'f444c4f62f88d18fa45b82c1265fdf09dca1c2f6';

abstract class _$DevMode extends $AsyncNotifier<bool> {
  FutureOr<bool> build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AsyncValue<bool>, bool>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<bool>, bool>,
              AsyncValue<bool>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
