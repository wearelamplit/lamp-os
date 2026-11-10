import 'package:flutter/material.dart';

/// Confirm-before-losing-edits dialog. Returns true to discard, false to
/// keep editing (barrier-dismiss counts as keep editing). Stays live under a
/// firmware push freeze, so a frozen editor can still be left.
Future<bool> confirmDiscard(BuildContext context) async {
  // Launched from the navigator's own context so it captures no launch-site
  // themes, the push lock's included. A revert write it leads to mid-push is
  // dropped by the lamp and settled by the re-read when the push ends.
  final result = await showDialog<bool>(
    context: Navigator.of(context).context,
    builder: (ctx) => AlertDialog(
      title: const Text('Discard changes?'),
      content: const Text('Your unsaved changes will be lost.'),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(ctx).pop(false),
          child: const Text('Keep editing'),
        ),
        FilledButton(
          onPressed: () => Navigator.of(ctx).pop(true),
          style: FilledButton.styleFrom(
            backgroundColor: Theme.of(ctx).colorScheme.error,
            foregroundColor: Theme.of(ctx).colorScheme.onError,
          ),
          child: const Text('Discard'),
        ),
      ],
    ),
  );
  return result ?? false;
}
