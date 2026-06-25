import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'lamp_save_status.g.dart';

/// Tracks whether a global Save Changes is currently in flight for a given
/// lamp. `control_notifier.save()` flips this true just before transitioning
/// state to AsyncLoading, and back to false once the post-reboot reconnect
/// resolves (or errors). `ConnectingView` watches it to switch its message
/// from "Connecting…" to "Saving changes…" during the post-save window so
/// the user knows the gap is intentional, not a connection problem.
@Riverpod(keepAlive: true, name: 'lampSaveStatusProvider')
class LampSaveStatus extends _$LampSaveStatus {
  @override
  bool build(String lampId) => false;

  void start() => state = true;
  void stop() => state = false;
}
