import 'package:flutter/material.dart';

/// Confirm-before-losing-edits dialog. Returns true to discard, false to
/// keep editing (barrier-dismiss counts as keep editing).
Future<bool> confirmDiscard(BuildContext context) async {
  final result = await showDialog<bool>(
    context: context,
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
