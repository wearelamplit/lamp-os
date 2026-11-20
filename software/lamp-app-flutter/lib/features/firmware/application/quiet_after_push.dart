import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'quiet_after_push.g.dart';

/// True while the lamp reports OTA quiet mode outside a push from this app,
/// so it drops every edit: after a push here ends without a reboot, or one
/// another session left behind. Holds until the lamp reports it is listening
/// or ControlNotifier.quietAfterPushBound passes. The lamp's ControlNotifier
/// sets it and keeps it alive.
@Riverpod(name: 'quietAfterPushProvider')
class QuietAfterPushNotifier extends _$QuietAfterPushNotifier {
  @override
  bool build(String lampId) => false;

  void set(bool quiet) => state = quiet;
}
