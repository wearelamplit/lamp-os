import 'dart:math';

// Whimsical connecting lines. State-neutral on purpose: shown for a first
// connect, a mid-session drop, and a save-reboot alike, so nothing may imply a
// prior connection. {name} is the lamp's display name.
const List<String> reachingLampLines = [
  'Reaching out to {name}…',
  'Coaxing {name} out of the shadows…',
  '{name} is playing hide-and-seek…',
  'Nudging {name} awake…',
  'Rounding up {name}…',
  'Luring {name} out with treats…',
  'Peeking around for {name}…',
  'Whistling for {name}…',
  "Trying to get {name}'s attention…",
  'Almost got {name}…',
];

String reachingLampLine(String template, String name) =>
    template.replaceAll('{name}', name);

// Emits every element once per shuffled pass before reshuffling, so no line
// repeats until the whole set has played.
class ShuffleBag<T> {
  ShuffleBag(List<T> items, this._rng) : _items = List<T>.from(items) {
    _shuffle();
  }

  final List<T> _items;
  final Random _rng;
  int _i = 0;

  void _shuffle() {
    _items.shuffle(_rng);
    _i = 0;
  }

  T next() {
    if (_i >= _items.length) _shuffle();
    return _items[_i++];
  }
}
