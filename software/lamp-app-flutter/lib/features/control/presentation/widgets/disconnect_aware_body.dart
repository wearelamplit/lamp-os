import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/control_notifier.dart';
import 'connection_banner.dart';

/// Greys + ignores `child` and renders a [ConnectionBanner] at the top
/// when the BLE link to `lampId` has dropped mid-session. Lifts the
/// disconnect-handling pattern that lives inline in ControlScreen /
/// ExpressionsScreen / ExpressionEditorScreen so the rest of the per-
/// lamp screens can opt in without duplicating the .select + banner +
/// IgnorePointer + Opacity wiring.
///
/// `child` stays mounted across the disconnect/reconnect so any in-flight
/// editor state survives — only its interactivity is suppressed. Wrap
/// the screen's body content; widgets that sit OUTSIDE the body (FABs,
/// AppBar action buttons that fire lamp writes) still need to be
/// disabled separately by reading the same connection slice at their
/// callsite.
class DisconnectAwareBody extends ConsumerWidget {
  const DisconnectAwareBody({
    super.key,
    required this.lampId,
    required this.child,
  });

  final String lampId;
  final Widget child;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conn = ref.watch(controlNotifierProvider(lampId).select((a) {
      final s = a.value;
      return (
        connected: s?.connected ?? true,
        attempt: s?.reconnectAttempt ?? 0,
      );
    }));
    final connected = conn.connected;
    return Column(
      children: [
        if (!connected) ConnectionBanner(attempt: conn.attempt),
        Expanded(
          child: IgnorePointer(
            ignoring: !connected,
            child: Opacity(
              opacity: connected ? 1.0 : 0.4,
              child: child,
            ),
          ),
        ),
      ],
    );
  }
}
