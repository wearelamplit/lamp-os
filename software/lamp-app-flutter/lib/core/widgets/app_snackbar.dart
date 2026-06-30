import 'package:flutter/material.dart';

/// Centralised snackbar durations and call sites so the app's cadence is
/// consistent and tunable in one place.
///
/// ```dart
/// AppSnackbar.info(context, 'WiFi creds sent to wisp.');
/// AppSnackbar.error(context, "Couldn't reach the wisp.");
/// AppSnackbar.action(context, message: 'Removed "breathing"',
///     actionLabel: 'UNDO', onAction: () => notifier.upsertExpression(e));
/// ```
///
/// Callers are responsible for `context.mounted`; these don't re-check.
abstract class AppSnackbar {
  /// Short informational snackbar; the user reads it and moves on.
  static const Duration infoDuration = Duration(seconds: 2);

  /// Non-actionable failures; longer so the user can read the cause.
  static const Duration errorDuration = Duration(seconds: 3);

  /// Long enough to decide whether to tap UNDO before self-dismiss.
  static const Duration actionDuration = Duration(seconds: 4);

  static void info(BuildContext context, String message) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), duration: infoDuration),
    );
  }

  static void error(BuildContext context, String message) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), duration: errorDuration),
    );
  }

  static void action(
    BuildContext context, {
    required String message,
    required String actionLabel,
    required VoidCallback onAction,
  }) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        duration: actionDuration,
        action: SnackBarAction(label: actionLabel, onPressed: onAction),
      ),
    );
  }
}
