// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'lamp_save_status.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
/// for a given lamp (e.g. password or advanced-LED changes that trigger a
/// firmware reboot). Flipped true just before state transitions to
/// AsyncLoading, back to false once the post-reboot reconnect resolves (or
/// errors). `ConnectingView` watches it to switch its message from
/// "Connecting…" to "Saving changes…" during the reconnect window so the
/// user knows the gap is intentional, not a connection problem.

@ProviderFor(LampSaveStatus)
final lampSaveStatusProvider = LampSaveStatusFamily._();

/// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
/// for a given lamp (e.g. password or advanced-LED changes that trigger a
/// firmware reboot). Flipped true just before state transitions to
/// AsyncLoading, back to false once the post-reboot reconnect resolves (or
/// errors). `ConnectingView` watches it to switch its message from
/// "Connecting…" to "Saving changes…" during the reconnect window so the
/// user knows the gap is intentional, not a connection problem.
final class LampSaveStatusProvider
    extends $NotifierProvider<LampSaveStatus, bool> {
  /// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
  /// for a given lamp (e.g. password or advanced-LED changes that trigger a
  /// firmware reboot). Flipped true just before state transitions to
  /// AsyncLoading, back to false once the post-reboot reconnect resolves (or
  /// errors). `ConnectingView` watches it to switch its message from
  /// "Connecting…" to "Saving changes…" during the reconnect window so the
  /// user knows the gap is intentional, not a connection problem.
  LampSaveStatusProvider._({
    required LampSaveStatusFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'lampSaveStatusProvider',
         isAutoDispose: false,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$lampSaveStatusHash();

  @override
  String toString() {
    return r'lampSaveStatusProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  LampSaveStatus create() => LampSaveStatus();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(bool value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<bool>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is LampSaveStatusProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$lampSaveStatusHash() => r'9c4ee0158d0c4774127ff48c1028863ec09d02f0';

/// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
/// for a given lamp (e.g. password or advanced-LED changes that trigger a
/// firmware reboot). Flipped true just before state transitions to
/// AsyncLoading, back to false once the post-reboot reconnect resolves (or
/// errors). `ConnectingView` watches it to switch its message from
/// "Connecting…" to "Saving changes…" during the reconnect window so the
/// user knows the gap is intentional, not a connection problem.

final class LampSaveStatusFamily extends $Family
    with $ClassFamilyOverride<LampSaveStatus, bool, bool, bool, String> {
  LampSaveStatusFamily._()
    : super(
        retry: null,
        name: r'lampSaveStatusProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: false,
      );

  /// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
  /// for a given lamp (e.g. password or advanced-LED changes that trigger a
  /// firmware reboot). Flipped true just before state transitions to
  /// AsyncLoading, back to false once the post-reboot reconnect resolves (or
  /// errors). `ConnectingView` watches it to switch its message from
  /// "Connecting…" to "Saving changes…" during the reconnect window so the
  /// user knows the gap is intentional, not a connection problem.

  LampSaveStatusProvider call(String lampId) =>
      LampSaveStatusProvider._(argument: lampId, from: this);

  @override
  String toString() => r'lampSaveStatusProvider';
}

/// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
/// for a given lamp (e.g. password or advanced-LED changes that trigger a
/// firmware reboot). Flipped true just before state transitions to
/// AsyncLoading, back to false once the post-reboot reconnect resolves (or
/// errors). `ConnectingView` watches it to switch its message from
/// "Connecting…" to "Saving changes…" during the reconnect window so the
/// user knows the gap is intentional, not a connection problem.

abstract class _$LampSaveStatus extends $Notifier<bool> {
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
