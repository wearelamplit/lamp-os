import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'ota_busy_seen_provider.g.dart';

/// A busy observation is trusted this long after the last adv that carried
/// the OTA-distributing bit. An OTA wave runs ~1 minute; past this the lamp
/// is almost certainly reachable again, so a connect failure is a genuine
/// error, not the busy state.
const _busyTtl = Duration(seconds: 90);

/// In-memory record of which lamps were last seen sourcing firmware, keyed by
/// BLE device id. Fed from the scan pipeline while the picker is open, then
/// read by the control screen after a connect fails so a doomed connect to a
/// busy lamp reads as "teaching, come back soon" instead of a generic error.
/// Kept alive (not scoped to the scanner) so the observation survives the
/// navigation from the picker into the lamp's screen.
@Riverpod(keepAlive: true, name: 'otaBusySeenProvider')
class OtaBusySeen extends _$OtaBusySeen {
  @override
  Map<String, int> build() => const {};

  /// Record an adv's OTA-distributing bit: stamp `now` when set, drop the id
  /// when clear so a finished wave stops reading as busy.
  void observe(String deviceId, {required bool distributing}) {
    final present = state.containsKey(deviceId);
    if (distributing) {
      state = {...state, deviceId: DateTime.now().millisecondsSinceEpoch};
    } else if (present) {
      state = {...state}..remove(deviceId);
    }
  }

  /// True when this lamp advertised busy within the trust window.
  bool wasBusyRecently(String deviceId) {
    final ts = state[deviceId];
    if (ts == null) return false;
    return DateTime.now().millisecondsSinceEpoch - ts < _busyTtl.inMilliseconds;
  }
}
