/// Counts taps inside a sliding window and fires [onTriggered] once
/// [count] taps land within [window]. Used for the "tap the lamplit
/// logo 5 times" gesture that unlocks advanced settings — ported from
/// the Vue app's `useTapCounter.ts`.
class TapCounter {
  TapCounter({
    required this.count,
    required this.window,
    required this.onTriggered,
  });

  final int count;
  final Duration window;
  final void Function() onTriggered;

  final List<DateTime> _taps = [];

  void record() {
    final now = DateTime.now();
    _taps
      ..removeWhere((t) => now.difference(t) > window)
      ..add(now);
    if (_taps.length >= count) {
      _taps.clear();
      onTriggered();
    }
  }
}
