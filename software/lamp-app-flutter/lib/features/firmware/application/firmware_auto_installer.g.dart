// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'firmware_auto_installer.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Auto-sends a newer cached image the moment a lamp connects, so the user
/// never taps Install. Kept alive by LampShell's watch; disposed (latch
/// reset) when the shell unmounts, giving a fresh evaluation on the next
/// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
/// the version gate makes a rebooted lamp ineligible on its own.

@ProviderFor(FirmwareAutoInstaller)
final firmwareAutoInstallerProvider = FirmwareAutoInstallerFamily._();

/// Auto-sends a newer cached image the moment a lamp connects, so the user
/// never taps Install. Kept alive by LampShell's watch; disposed (latch
/// reset) when the shell unmounts, giving a fresh evaluation on the next
/// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
/// the version gate makes a rebooted lamp ineligible on its own.
final class FirmwareAutoInstallerProvider
    extends $NotifierProvider<FirmwareAutoInstaller, void> {
  /// Auto-sends a newer cached image the moment a lamp connects, so the user
  /// never taps Install. Kept alive by LampShell's watch; disposed (latch
  /// reset) when the shell unmounts, giving a fresh evaluation on the next
  /// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
  /// the version gate makes a rebooted lamp ineligible on its own.
  FirmwareAutoInstallerProvider._({
    required FirmwareAutoInstallerFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'firmwareAutoInstallerProvider',
         isAutoDispose: true,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$firmwareAutoInstallerHash();

  @override
  String toString() {
    return r'firmwareAutoInstallerProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  FirmwareAutoInstaller create() => FirmwareAutoInstaller();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(void value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<void>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is FirmwareAutoInstallerProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$firmwareAutoInstallerHash() =>
    r'9244117ac002107aefd0f5095c675c12bd150279';

/// Auto-sends a newer cached image the moment a lamp connects, so the user
/// never taps Install. Kept alive by LampShell's watch; disposed (latch
/// reset) when the shell unmounts, giving a fresh evaluation on the next
/// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
/// the version gate makes a rebooted lamp ineligible on its own.

final class FirmwareAutoInstallerFamily extends $Family
    with $ClassFamilyOverride<FirmwareAutoInstaller, void, void, void, String> {
  FirmwareAutoInstallerFamily._()
    : super(
        retry: null,
        name: r'firmwareAutoInstallerProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: true,
      );

  /// Auto-sends a newer cached image the moment a lamp connects, so the user
  /// never taps Install. Kept alive by LampShell's watch; disposed (latch
  /// reset) when the shell unmounts, giving a fresh evaluation on the next
  /// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
  /// the version gate makes a rebooted lamp ineligible on its own.

  FirmwareAutoInstallerProvider call(String deviceId) =>
      FirmwareAutoInstallerProvider._(argument: deviceId, from: this);

  @override
  String toString() => r'firmwareAutoInstallerProvider';
}

/// Auto-sends a newer cached image the moment a lamp connects, so the user
/// never taps Install. Kept alive by LampShell's watch; disposed (latch
/// reset) when the shell unmounts, giving a fresh evaluation on the next
/// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
/// the version gate makes a rebooted lamp ineligible on its own.

abstract class _$FirmwareAutoInstaller extends $Notifier<void> {
  late final _$args = ref.$arg as String;
  String get deviceId => _$args;

  void build(String deviceId);
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<void, void>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<void, void>,
              void,
              Object?,
              Object?
            >;
    element.handleCreate(ref, () => build(_$args));
  }
}
