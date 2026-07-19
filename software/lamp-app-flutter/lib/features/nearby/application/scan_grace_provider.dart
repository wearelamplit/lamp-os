import 'dart:async';

import 'package:riverpod_annotation/riverpod_annotation.dart';

part 'scan_grace_provider.g.dart';

/// Grace window during which an inventoried lamp not yet heard
/// shows as `searching` rather than `offline`. ~5× the typical 1 Hz BLE
/// adv interval, long enough for every in-range lamp to have landed at
/// least once.
const _scanGrace = Duration(seconds: 5);

/// `true` for [_scanGrace] from the moment something first watches this
/// provider, then `false` for as long as a watcher stays mounted.
///
/// Auto-dispose so the Timer is torn down together with the widgets
/// that care about it. Without this, widget tests (which sync-dispose
/// the container after the test body returns) would trip on
/// `!timersPending`. The trade-off is that if every watcher unmounts
/// and a new one mounts later, the grace re-arms; that mirrors the
/// user-facing intent: a list just opened, give a beat to hear
/// in-range lamps before flagging them offline.
@riverpod
class ScanGraceActive extends _$ScanGraceActive {
  Timer? _timer;

  @override
  bool build() {
    _timer = Timer(_scanGrace, () {
      _timer = null;
      state = false;
    });
    ref.onDispose(() {
      _timer?.cancel();
      _timer = null;
    });
    return true;
  }
}
