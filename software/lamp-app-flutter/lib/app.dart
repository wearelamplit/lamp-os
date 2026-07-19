import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'core/ble/ble_permissions.dart';
import 'core/ble/bluetooth_gate.dart';
import 'core/ble/reaching_lamp_gate.dart';
import 'core/lifecycle/app_lifecycle.dart';
import 'core/routing/router.dart';
import 'core/theme/app_theme.dart';
import 'core/theme/brand.dart';

class LampApp extends ConsumerStatefulWidget {
  const LampApp({super.key, BlePermissions? permissions})
      : _injected = permissions;
  final BlePermissions? _injected;

  @override
  ConsumerState<LampApp> createState() => _LampAppState();
}

class _LampAppState extends ConsumerState<LampApp>
    with WidgetsBindingObserver {
  late final BlePermissions _perms =
      widget._injected ?? BlePermissions.forPlatform();
  Future<bool>? _granted;
  /// Cached resolved value of `_granted`. Used to avoid replacing
  /// `_granted` with a fresh future on every resume, which would make
  /// FutureBuilder flicker through its loading state and unmount the
  /// entire MaterialApp.router, disposing LampShell and ControlNotifier
  /// every time the user unlocked their phone. Only re-checks the
  /// permission when it isn't already known to be granted (i.e. the user
  /// might have toggled it off in Settings between resume cycles).
  bool _knownGranted = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _granted = _perms.request().then((v) {
      _knownGranted = v;
      return v;
    });
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // Broadcast lifecycle to the rest of the app via Riverpod. Currently
    // consumed by ControlNotifier to probe the BLE link on resume and
    // kick a reconnect if it died in the background. The OS will
    // silently drop BLE connections after the app has been Stopped for
    // a while, and fbp's `connectionState` stream doesn't always emit
    // the `disconnected` edge when the process wakes back up.
    ref.read(appLifecycleStateProvider.notifier).set(state);

    // App came back to foreground. If the user just toggled BT perm
    // in Settings (post-`openSettings`), the in-memory `_granted`
    // Future still resolves to false until it re-checks. Without this,
    // the user grants the permission, switches back to the app, and
    // the screen still says "Allow Bluetooth" until they tap it.
    //
    // Re-checks ONLY when the permission isn't already known to be
    // granted. Once `_knownGranted` is true, the user can't have
    // *gained* something worth re-confirming. Avoiding the setState
    // is critical: replacing `_granted` with a new Future makes the
    // FutureBuilder above flicker through its loading state, which
    // unmounts the MaterialApp.router, which disposes LampShell, which
    // disposes ControlNotifier. The user would then see a full-page
    // "Couldn't reach your lamp" error on every screen-unlock after a
    // brief reconnect window. This guard keeps the resume-on-Settings
    // recovery path AND lets backgrounded lamp connections survive.
    if (state == AppLifecycleState.resumed && !_knownGranted) {
      setState(() {
        _granted = _perms.isGranted().then((v) {
          _knownGranted = v;
          return v;
        });
      });
    }
  }

  void _retry() {
    setState(() {
      _granted = _requestOrOpenSettings().then((v) {
        _knownGranted = v;
        return v;
      });
    });
  }

  Future<bool> _requestOrOpenSettings() async {
    final granted = await _perms.request();
    if (granted) return true;
    // Android marks the permission "permanently denied" after the user has
    // dismissed the system prompt twice. iOS treats the first deny the
    // same way; only Settings can recover. From there, request() returns
    // false immediately without showing anything; the only path forward
    // is the app-settings screen.
    if (await _perms.isPermanentlyDenied()) {
      await _perms.openSettings();
      // didChangeAppLifecycleState picks up the resume and re-evaluates
      // anyway; the inline isGranted call here is the immediate response
      // for callers that don't go through the lifecycle path.
      return _perms.isGranted();
    }
    return false;
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'LampOS',
      theme: appTheme,
      darkTheme: appTheme,
      themeMode: ThemeMode.dark,
      debugShowCheckedModeBanner: false,
      home: FutureBuilder<bool>(
        future: _granted,
        builder: (context, snap) {
          if (snap.connectionState != ConnectionState.done) {
            return const Scaffold(
              body: Center(child: CircularProgressIndicator()),
            );
          }
          if (snap.data != true) {
            return Scaffold(
              body: Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Text(
                      'Bluetooth permission needed',
                      style: TextStyle(
                        color: Brand.lampWhite,
                        fontWeight: FontWeight.w600,
                        fontSize: 18,
                      ),
                    ),
                    const SizedBox(height: 12),
                    const Text(
                      'LampOS talks to your lamps over Bluetooth.',
                      style: TextStyle(color: Brand.fogGrey),
                    ),
                    const SizedBox(height: 24),
                    FilledButton(
                      onPressed: _retry,
                      child: const Text('Allow Bluetooth'),
                    ),
                  ],
                ),
              ),
            );
          }
          final router = ref.watch(appRouterProvider);
          return MaterialApp.router(
            title: 'LampOS',
            theme: appTheme,
            darkTheme: appTheme,
            themeMode: ThemeMode.dark,
            routerConfig: router,
            builder: (context, child) => BluetoothGate(
              child: ReachingLampGate(
                  child: child ?? const SizedBox.shrink()),
            ),
            debugShowCheckedModeBanner: false,
          );
        },
      ),
    );
  }
}
