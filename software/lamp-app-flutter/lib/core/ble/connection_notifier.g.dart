// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'connection_notifier.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning

@ProviderFor(ConnectionNotifier)
final connectionNotifierProvider = ConnectionNotifierFamily._();

final class ConnectionNotifierProvider
    extends $NotifierProvider<ConnectionNotifier, ConnectionStatus> {
  ConnectionNotifierProvider._({
    required ConnectionNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'connectionNotifierProvider',
         isAutoDispose: false,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$connectionNotifierHash();

  @override
  String toString() {
    return r'connectionNotifierProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  ConnectionNotifier create() => ConnectionNotifier();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(ConnectionStatus value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<ConnectionStatus>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is ConnectionNotifierProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$connectionNotifierHash() =>
    r'770d220fe0db186e4d2090c40345f232b511f6e4';

final class ConnectionNotifierFamily extends $Family
    with
        $ClassFamilyOverride<
          ConnectionNotifier,
          ConnectionStatus,
          ConnectionStatus,
          ConnectionStatus,
          String
        > {
  ConnectionNotifierFamily._()
    : super(
        retry: null,
        name: r'connectionNotifierProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: false,
      );

  ConnectionNotifierProvider call(String deviceId) =>
      ConnectionNotifierProvider._(argument: deviceId, from: this);

  @override
  String toString() => r'connectionNotifierProvider';
}

abstract class _$ConnectionNotifier extends $Notifier<ConnectionStatus> {
  late final _$args = ref.$arg as String;
  String get deviceId => _$args;

  ConnectionStatus build(String deviceId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<ConnectionStatus, ConnectionStatus>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<ConnectionStatus, ConnectionStatus>,
              ConnectionStatus,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
