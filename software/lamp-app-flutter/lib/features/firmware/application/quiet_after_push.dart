import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'quiet_after_push.g.dart';

/// True while a push to the lamp has ended without a reboot and the lamp
/// still reports OTA quiet mode, so it drops every edit. The lamp's
/// ControlNotifier sets it and keeps it alive.
@Riverpod(name: 'quietAfterPushProvider')
class QuietAfterPushNotifier extends _$QuietAfterPushNotifier {
  @override
  bool build(String lampId) => false;

  void set(bool quiet) => state = quiet;
}
