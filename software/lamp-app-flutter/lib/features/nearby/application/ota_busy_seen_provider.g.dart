// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'ota_busy_seen_provider.dart';

// **************************************************************************
// RiverpodGenerator
// **************************************************************************

// GENERATED CODE - DO NOT MODIFY BY HAND
// ignore_for_file: type=lint, type=warning
/// In-memory record of which lamps were last seen sourcing firmware, keyed by
/// BLE device id. Fed from the scan pipeline while the picker is open, then
/// read by the control screen after a connect fails so a doomed connect to a
/// busy lamp reads as "teaching, come back soon" instead of a generic error.
/// Kept alive (not scoped to the scanner) so the observation survives the
/// navigation from the picker into the lamp's screen.

@ProviderFor(OtaBusySeen)
final otaBusySeenProvider = OtaBusySeenProvider._();

/// In-memory record of which lamps were last seen sourcing firmware, keyed by
/// BLE device id. Fed from the scan pipeline while the picker is open, then
/// read by the control screen after a connect fails so a doomed connect to a
/// busy lamp reads as "teaching, come back soon" instead of a generic error.
/// Kept alive (not scoped to the scanner) so the observation survives the
/// navigation from the picker into the lamp's screen.
final class OtaBusySeenProvider
    extends $NotifierProvider<OtaBusySeen, Map<String, int>> {
  /// In-memory record of which lamps were last seen sourcing firmware, keyed by
  /// BLE device id. Fed from the scan pipeline while the picker is open, then
  /// read by the control screen after a connect fails so a doomed connect to a
  /// busy lamp reads as "teaching, come back soon" instead of a generic error.
  /// Kept alive (not scoped to the scanner) so the observation survives the
  /// navigation from the picker into the lamp's screen.
  OtaBusySeenProvider._()
    : super(
        from: null,
        argument: null,
        retry: null,
        name: r'otaBusySeenProvider',
        isAutoDispose: false,
        dependencies: null,
        $allTransitiveDependencies: null,
      );

  @override
  String debugGetCreateSourceHash() => _$otaBusySeenHash();

  @$internal
  @override
  OtaBusySeen create() => OtaBusySeen();

  /// {@macro riverpod.override_with_value}
  Override overrideWithValue(Map<String, int> value) {
    return $ProviderOverride(
      origin: this,
      providerOverride: $SyncValueProvider<Map<String, int>>(value),
    );
  }
}

String _$otaBusySeenHash() => r'dddf65f3bc6ec6c1f02048e8b28a089f89175717';

/// In-memory record of which lamps were last seen sourcing firmware, keyed by
/// BLE device id. Fed from the scan pipeline while the picker is open, then
/// read by the control screen after a connect fails so a doomed connect to a
/// busy lamp reads as "teaching, come back soon" instead of a generic error.
/// Kept alive (not scoped to the scanner) so the observation survives the
/// navigation from the picker into the lamp's screen.

abstract class _$OtaBusySeen extends $Notifier<Map<String, int>> {
  Map<String, int> build();
  @$mustCallSuper
  @override
  void runBuild() {
    final ref = this.ref as $Ref<Map<String, int>, Map<String, int>>;
    final element =
        ref.element
            as $ClassProviderElement<
              AnyNotifier<Map<String, int>, Map<String, int>>,
              Map<String, int>,
              Object?,
              Object?
            >;
    element.handleCreate(ref, build);
  }
}
