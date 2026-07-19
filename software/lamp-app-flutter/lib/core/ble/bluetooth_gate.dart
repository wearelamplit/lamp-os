import 'dart:io' show Platform;

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:permission_handler/permission_handler.dart';

import '../theme/app_spacing.dart';
import '../theme/brand.dart';

/// Live BLE adapter power state. `off` and `unauthorized` raise the app-wide
/// [BluetoothGate]; every other state passes through. `flutter test` has no
/// platform adapter, so emit `on` to keep the gate clear in widget tests.
final bluetoothAdapterStateProvider =
    StreamProvider<fbp.BluetoothAdapterState>((ref) {
  if (Platform.environment.containsKey('FLUTTER_TEST')) {
    return Stream.value(fbp.BluetoothAdapterState.on);
  }
  return fbp.FlutterBluePlus.adapterState;
});

/// Blocking overlay mounted above the router. Covers any routed screen
/// (including a live control session whose GATT link just died) while the BLE
/// adapter is off or unauthorized, and uncovers it the instant the adapter
/// recovers. Overlays rather than replaces so the router subtree (and its
/// ControlNotifier reconnect ladders) stays mounted underneath.
class BluetoothGate extends ConsumerWidget {
  const BluetoothGate({super.key, required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final overlay = _overlayFor(ref.watch(bluetoothAdapterStateProvider).value);
    return Stack(
      children: [
        child,
        if (overlay != null) Positioned.fill(child: overlay),
      ],
    );
  }

  Widget? _overlayFor(fbp.BluetoothAdapterState? state) {
    switch (state) {
      case fbp.BluetoothAdapterState.off:
        return const _GatePanel(
          icon: Icons.bluetooth_disabled,
          title: 'Bluetooth is off',
          message: 'LampOS talks to your lamps over Bluetooth.',
          action: _GateAction.turnOn,
        );
      case fbp.BluetoothAdapterState.unauthorized:
        return const _GatePanel(
          icon: Icons.lock_outline,
          title: 'Bluetooth permission needed',
          message:
              'Allow Bluetooth for LampOS in Settings to reach your lamps.',
          action: _GateAction.openSettings,
        );
      default:
        return null;
    }
  }
}

enum _GateAction { turnOn, openSettings }

class _GatePanel extends StatelessWidget {
  const _GatePanel({
    required this.icon,
    required this.title,
    required this.message,
    required this.action,
  });

  final IconData icon;
  final String title;
  final String message;
  final _GateAction action;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Padding(
          padding: const EdgeInsets.all(AppSpace.xl),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                icon,
                size: 48,
                color: Brand.fogGrey,
              ),
              const SizedBox(height: AppSpace.lg),
              Text(
                title,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  color: Brand.lampWhite,
                  fontWeight: FontWeight.w600,
                  fontSize: 18,
                ),
              ),
              const SizedBox(height: AppSpace.md),
              Text(
                message,
                textAlign: TextAlign.center,
                style: const TextStyle(color: Brand.fogGrey),
              ),
              const SizedBox(height: AppSpace.xl),
              ..._actionWidgets(),
            ],
          ),
        ),
      ),
    );
  }

  List<Widget> _actionWidgets() {
    switch (action) {
      case _GateAction.turnOn:
        // FlutterBluePlus.turnOn() is Android-only; iOS gets instructions.
        if (Platform.isAndroid) {
          return [
            FilledButton(
              onPressed: () async {
                try {
                  await fbp.FlutterBluePlus.turnOn();
                } catch (_) {}
              },
              child: const Text('Turn on Bluetooth'),
            ),
          ];
        }
        return const [
          Text(
            'Turn Bluetooth back on from Control Center or Settings.',
            textAlign: TextAlign.center,
            style: TextStyle(color: Brand.fogGrey),
          ),
        ];
      case _GateAction.openSettings:
        return const [
          FilledButton(
            onPressed: openAppSettings,
            child: Text('Open Settings'),
          ),
        ];
    }
  }
}
