// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'control_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(ControlNotifier)
final controlNotifierProvider = ControlNotifierFamily._();

final class ControlNotifierProvider
    extends $AsyncNotifierProvider<ControlNotifier, ControlState> {
  ControlNotifierProvider._({
    required ControlNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: _noRetry,
         name: r'controlNotifierProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$controlNotifierHash();

  @override
  String toString() {
    return r'controlNotifierProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  ControlNotifier create() => ControlNotifier();

  @override
  bool operator ==(Object other) {
    return other is ControlNotifierProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$controlNotifierHash() => r'c1cf41b7adc52411ea8295c21002bc42bf4cd997';

final class ControlNotifierFamily extends $Family
    with
        $ClassFamilyOverride<
          ControlNotifier,
          AsyncValue<ControlState>,
          ControlState,
          FutureOr<ControlState>,
          String
        > {
  ControlNotifierFamily._()
    : super(
        retry: _noRetry,
        name: r'controlNotifierProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  ControlNotifierProvider call(String deviceId) =>
      ControlNotifierProvider._(argument: deviceId, from: this);

  @override
  String toString() => r'controlNotifierProvider';
}

abstract class _$ControlNotifier extends $AsyncNotifier<ControlState> {
  late final _$args = ref.$arg as String;
  String get deviceId => _$args;

  FutureOr<ControlState> build(String deviceId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<AsyncValue<ControlState>, ControlState>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<AsyncValue<ControlState>, ControlState>,
              AsyncValue<ControlState>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
