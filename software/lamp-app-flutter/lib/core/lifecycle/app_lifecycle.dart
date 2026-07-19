import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

/// Broadcasts the current `AppLifecycleState` to anything that wants to
/// react to the app being foregrounded / backgrounded / detached.
///
/// Wired from `LampApp.didChangeAppLifecycleState`, the only
/// `WidgetsBindingObserver` in the app. Single source of truth so
/// any number of notifiers (currently: ControlNotifier's probe-on-resume
/// reconnect path) can listen without each holding its own observer.
///
/// Seed is `resumed`: by the time anything reads this in production the
/// app has finished its first frame, so `resumed` is the truthful start
/// state. Tests can override the seed via ProviderScope as needed.
///
/// Riverpod 3 removed `StateProvider`; the smallest Notifier shape that
/// replaces it lives here so call sites can stay `ref.read(provider
/// .notifier).set(...)` / `ref.listen(provider, ...)`.
class AppLifecycleNotifier extends Notifier<AppLifecycleState> {
  @override
  AppLifecycleState build() => AppLifecycleState.resumed;

  void set(AppLifecycleState newState) {
    state = newState;
  }
}

final appLifecycleStateProvider =
    NotifierProvider<AppLifecycleNotifier, AppLifecycleState>(
        AppLifecycleNotifier.new);
