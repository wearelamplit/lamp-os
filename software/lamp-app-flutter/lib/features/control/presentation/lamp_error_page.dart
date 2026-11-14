import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/widgets/friendly_error.dart';
import '../application/control_notifier.dart';
import '../application/lamp_busy_exception.dart';

/// The error page for a lamp screen whose `controlNotifierProvider` failed,
/// with a retry that reloads the lamp.
class LampErrorPage extends ConsumerWidget {
  const LampErrorPage({super.key, required this.lampId, required this.error});

  final String lampId;
  final Object error;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    void retry() => ref.invalidate(controlNotifierProvider(lampId));
    if (error is LampBusyException) {
      return FriendlyError.page(
        title: 'Your lamp is low on memory.',
        subtitle: 'Give it a moment, then try again.',
        rawError: error,
        onRetry: retry,
      );
    }
    return FriendlyError.page(
      title: "Couldn't reach your lamp.",
      subtitle:
          "They may have wandered out of range. Bring your phone closer "
          'and try again.',
      rawError: error,
      onRetry: retry,
    );
  }
}
