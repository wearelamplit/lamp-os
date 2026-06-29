import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'lamp_save_status.g.dart';

/// Tracks whether a `reboot:true` `writeSettingsBlob` is currently in flight
/// for a given lamp (e.g. password or advanced-LED changes that trigger a
/// firmware reboot). Flipped true just before state transitions to
/// AsyncLoading, back to false once the post-reboot reconnect resolves (or
/// errors). `ConnectingView` watches it to switch its message from
/// "Connecting…" to "Saving changes…" during the reconnect window so the
/// user knows the gap is intentional, not a connection problem.
@Riverpod(keepAlive: true, name: 'lampSaveStatusProvider')
class LampSaveStatus extends _$LampSaveStatus {
  @override
  bool build(String lampId) => false;

  void start() => state = true;
  void stop() => state = false;
}
