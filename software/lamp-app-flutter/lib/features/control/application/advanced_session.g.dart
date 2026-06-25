// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'advanced_session.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// Session-only "advanced mode" flag per lamp. Holds whether the user has
/// unlocked advanced UI in the current connection session.
///
/// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
/// which the lamp itself stores in NVS — this provider is purely
/// app-side state, scoped to the current BLE connection session, and
/// resets to `false` whenever that session ends (handled by
/// `ControlNotifier._onConnectionChange(false)`).
///
/// Gates visibility of advanced UI like the expression cascade controls
/// and the Setup Hub's Advanced LED setup row. The actual feature state
/// (cascade params, LED config) lives in the lamp config and persists
/// across sessions independently of this flag — this only controls
/// whether the controls are visible.

@ProviderFor(AdvancedSession)
final advancedSessionProvider = AdvancedSessionFamily._();

/// Session-only "advanced mode" flag per lamp. Holds whether the user has
/// unlocked advanced UI in the current connection session.
///
/// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
/// which the lamp itself stores in NVS — this provider is purely
/// app-side state, scoped to the current BLE connection session, and
/// resets to `false` whenever that session ends (handled by
/// `ControlNotifier._onConnectionChange(false)`).
///
/// Gates visibility of advanced UI like the expression cascade controls
/// and the Setup Hub's Advanced LED setup row. The actual feature state
/// (cascade params, LED config) lives in the lamp config and persists
/// across sessions independently of this flag — this only controls
/// whether the controls are visible.
final class AdvancedSessionProvider
    extends $NotifierProvider<AdvancedSession, bool> {
  /// Session-only "advanced mode" flag per lamp. Holds whether the user has
  /// unlocked advanced UI in the current connection session.
  ///
  /// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
  /// which the lamp itself stores in NVS — this provider is purely
  /// app-side state, scoped to the current BLE connection session, and
  /// resets to `false` whenever that session ends (handled by
  /// `ControlNotifier._onConnectionChange(false)`).
  ///
  /// Gates visibility of advanced UI like the expression cascade controls
  /// and the Setup Hub's Advanced LED setup row. The actual feature state
  /// (cascade params, LED config) lives in the lamp config and persists
  /// across sessions independently of this flag — this only controls
  /// whether the controls are visible.
  AdvancedSessionProvider._({
    required AdvancedSessionFamily super.from,
    required String super.argument,
  }) : super(
         retry: null,
         name: r'advancedSessionProvider',
         isAutoDispose: false,
         dependencies: null,
         $allTransitiveDependencies: null,
       );

  @override
  String debugGetCreateSourceHash() => _$advancedSessionHash();

  @override
  String toString() {
    return r'advancedSessionProvider'
        ''
        '($argument)';
  }

  @$internal
  @override
  AdvancedSession create() => AdvancedSession();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(bool value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<bool>(value),
    );
  }

  @override
  bool operator ==(Object other) {
    return other is AdvancedSessionProvider && other.argument == argument;
  }

  @override
  int get hashCode {
    return argument.hashCode;
  }
}

String _$advancedSessionHash() => r'aa5f5735731487ad8cd991b03ffd6212e4c13e11';

/// Session-only "advanced mode" flag per lamp. Holds whether the user has
/// unlocked advanced UI in the current connection session.
///
/// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
/// which the lamp itself stores in NVS — this provider is purely
/// app-side state, scoped to the current BLE connection session, and
/// resets to `false` whenever that session ends (handled by
/// `ControlNotifier._onConnectionChange(false)`).
///
/// Gates visibility of advanced UI like the expression cascade controls
/// and the Setup Hub's Advanced LED setup row. The actual feature state
/// (cascade params, LED config) lives in the lamp config and persists
/// across sessions independently of this flag — this only controls
/// whether the controls are visible.

final class AdvancedSessionFamily extends $Family
    with $ClassFamilyOverride<AdvancedSession, bool, bool, bool, String> {
  AdvancedSessionFamily._()
    : super(
        retry: null,
        name: r'advancedSessionProvider',
        dependencies: null,
        $allTransitiveDependencies: null,
        isAutoDispose: false,
      );

  /// Session-only "advanced mode" flag per lamp. Holds whether the user has
  /// unlocked advanced UI in the current connection session.
  ///
  /// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
  /// which the lamp itself stores in NVS — this provider is purely
  /// app-side state, scoped to the current BLE connection session, and
  /// resets to `false` whenever that session ends (handled by
  /// `ControlNotifier._onConnectionChange(false)`).
  ///
  /// Gates visibility of advanced UI like the expression cascade controls
  /// and the Setup Hub's Advanced LED setup row. The actual feature state
  /// (cascade params, LED config) lives in the lamp config and persists
  /// across sessions independently of this flag — this only controls
  /// whether the controls are visible.

  AdvancedSessionProvider call(String lampId) =>
      AdvancedSessionProvider._(argument: lampId, from: this);

  @override
  String toString() => r'advancedSessionProvider';
}

/// Session-only "advanced mode" flag per lamp. Holds whether the user has
/// unlocked advanced UI in the current connection session.
///
/// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
/// which the lamp itself stores in NVS — this provider is purely
/// app-side state, scoped to the current BLE connection session, and
/// resets to `false` whenever that session ends (handled by
/// `ControlNotifier._onConnectionChange(false)`).
///
/// Gates visibility of advanced UI like the expression cascade controls
/// and the Setup Hub's Advanced LED setup row. The actual feature state
/// (cascade params, LED config) lives in the lamp config and persists
/// across sessions independently of this flag — this only controls
/// whether the controls are visible.

abstract class _$AdvancedSession extends $Notifier<bool> {
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
