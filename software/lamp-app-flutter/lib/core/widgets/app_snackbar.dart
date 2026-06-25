import 'package:flutter/material.dart';

/// Centralises the snackbar cadence we use across the app (audit W8).
///
/// Before this helper, every call site reached for
/// `ScaffoldMessenger.of(context).showSnackBar(SnackBar(...))` and
/// picked its own duration — most sites used 2s, expressions used 4s
/// for the UNDO action affordance, wisp_pane omitted duration entirely
/// (defaulted to the framework's 4s). The mixed cadence felt
/// inconsistent (a transient "Copied" stayed on screen as long as an
/// actionable "Removed expression"), and tweaking the global cadence
/// meant touching ~12 sites.
///
/// Usage:
/// ```dart
/// AppSnackbar.info(context, 'WiFi creds sent to wisp.');
/// AppSnackbar.error(context, "Couldn't reach the wisp — try again.");
/// AppSnackbar.action(
///   context,
///   message: 'Removed "breathing"',
///   actionLabel: 'UNDO',
///   onAction: () => notifier.upsertExpression(e),
/// );
/// ```
///
/// All three honour `context.mounted` at the call site — they don't
/// re-check, since the caller usually has already awaited an async op
/// before deciding to surface the snackbar.
abstract class AppSnackbar {
  /// Short informational snackbar. 2s — the same cadence the existing
  /// "Copied $url" / "Advanced settings unlocked" sites used. The user
  /// reads it and moves on; nothing for them to do.
  static const Duration infoDuration = Duration(seconds: 2);

  /// Slightly longer for non-actionable failure messages so the user
  /// has time to read the cause.
  static const Duration errorDuration = Duration(seconds: 3);

  /// Long enough that the user can decide whether to tap UNDO before
  /// the snackbar self-dismisses. Matches the existing "Removed X"
  /// cadence in expressions_screen.
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
