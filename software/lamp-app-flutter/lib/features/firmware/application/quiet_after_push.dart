import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'quiet_after_push.g.dart';

/// True from a push to the lamp ending without a reboot while the lamp still
/// reports OTA quiet mode, so it drops every edit, until the lamp reports it
/// is listening or ControlNotifier.quietAfterPushBound passes. The lamp's
/// ControlNotifier sets it and keeps it alive.
@Riverpod(name: 'quietAfterPushProvider')
class QuietAfterPushNotifier extends _$QuietAfterPushNotifier {
  @override
  bool build(String lampId) => false;

  void set(bool quiet) => state = quiet;
}
