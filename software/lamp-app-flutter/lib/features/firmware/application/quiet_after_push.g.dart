// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'quiet_after_push.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// True from a push to the lamp ending without a reboot while the lamp still
/// reports OTA quiet mode, so it drops every edit, until the lamp reports it
/// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
/// ControlNotifier sets it and keeps it alive.

@ProviderFor(QuietAfterPushNotifier)
final quietAfterPushProvider = QuietAfterPushNotifierFamily._();

/// True from a push to the lamp ending without a reboot while the lamp still
/// reports OTA quiet mode, so it drops every edit, until the lamp reports it
/// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
/// ControlNotifier sets it and keeps it alive.
final class QuietAfterPushNotifierProvider
    extends $NotifierProvider<QuietAfterPushNotifier, bool> {
  /// True from a push to the lamp ending without a reboot while the lamp still
  /// reports OTA quiet mode, so it drops every edit, until the lamp reports it
  /// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
  /// ControlNotifier sets it and keeps it alive.
  QuietAfterPushNotifierProvider._({
    required QuietAfterPushNotifierFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'quietAfterPushProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$quietAfterPushNotifierHash();

  @override
  String toString() {
    return r'quietAfterPushProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  QuietAfterPushNotifier create() => QuietAfterPushNotifier();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(bool value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<bool>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is QuietAfterPushNotifierProvider &&
        other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$quietAfterPushNotifierHash() =>
    r'4c6402848a2cbdd0bb998907f384aec5e17868c7';

/// True from a push to the lamp ending without a reboot while the lamp still
/// reports OTA quiet mode, so it drops every edit, until the lamp reports it
/// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
/// ControlNotifier sets it and keeps it alive.

final class QuietAfterPushNotifierFamily extends $Family
    with
        $ClassFamilyOverride<QuietAfterPushNotifier, bool, bool, bool, String> {
  QuietAfterPushNotifierFamily._()
    : super(
        retry: null,
        name: r'quietAfterPushProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// True from a push to the lamp ending without a reboot while the lamp still
  /// reports OTA quiet mode, so it drops every edit, until the lamp reports it
  /// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
  /// ControlNotifier sets it and keeps it alive.

  QuietAfterPushNotifierProvider call(String lampId) =>
      QuietAfterPushNotifierProvider._(argument: lampId, from: this);

  @override
  String toString() => r'quietAfterPushProvider';
}

/// True from a push to the lamp ending without a reboot while the lamp still
/// reports OTA quiet mode, so it drops every edit, until the lamp reports it
/// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
/// ControlNotifier sets it and keeps it alive.

abstract class _$QuietAfterPushNotifier extends $Notifier<bool> {
  late final _$args = ref.$arg as String;
  String get lampId => _$args;

  bool build(String lampId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<bool, bool>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<bool, bool>,
              bool,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
