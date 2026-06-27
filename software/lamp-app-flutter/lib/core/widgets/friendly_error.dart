import 'package:flutter/material.dart';

import '../utils/tap_counter.dart';

/// Friendly error surface used wherever a Riverpod AsyncError or a caught
/// exception would otherwise dump `e.toString()` at the user. The raw
/// exception is hidden behind a 5-tap gesture on the message — matching
/// the same 5-tap convention used elsewhere (Info wordmark → advanced
/// settings unlock).
///
/// Two layouts:
///   - [FriendlyError.page] fills a Scaffold body with an icon + heading +
///     subtitle, used in `controlNotifier` `.when(error:)` branches.
///   - [FriendlyError.inline] is a compact text variant for surfacing
///     errors inside an existing form (onboarding password step,
///     connect-password dialog).
class FriendlyError extends StatefulWidget {
  const FriendlyError.page({
    super.key,
    required this.title,
    required this.subtitle,
    required this.rawError,
    this.onRetry,
  }) : _inline = false;

  const FriendlyError.inline({
    super.key,
    required this.title,
    required this.rawError,
    this.subtitle,
  })  : onRetry = null,
        _inline = true;

  /// Headline. Short, on-voice — e.g. "Couldn't reach your lamp."
  final String title;

  /// Optional supporting line under the headline. Tells the user what to
  /// try; keep this empty when there's nothing actionable to say.
  final String? subtitle;

  /// The underlying exception. Hidden until the user taps the title 5
  /// times within 3 seconds. Pass `null` if there's nothing to reveal.
  final Object? rawError;

  /// Renders a "Try again" button below the subtitle. Without this the
  /// only way out of the error page is to back out and re-enter — which
  /// for BLE-reach failures means restarting the app. Page-only.
  final VoidCallback? onRetry;

  final bool _inline;

  @override
  State<FriendlyError> createState() => _FriendlyErrorState();
}

class _FriendlyErrorState extends State<FriendlyError> {
  bool _revealed = false;
  late final TapCounter _tap = TapCounter(
    count: 5,
    window: const Duration(seconds: 3),
    onTriggered: () {
      if (widget.rawError == null) return;
      if (!mounted) return;
      setState(() => _revealed = true);
    },
  );

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final children = <Widget>[];
    if (!widget._inline) {
      children.add(Icon(
        Icons.cloud_off_outlined,
        color: colorScheme.onSurfaceVariant,
        size: 48,
      ));
      children.add(const SizedBox(height: 12));
    }
    children.add(GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: _tap.record,
      child: Text(
        widget.title,
        textAlign: TextAlign.center,
        style: TextStyle(
          color: widget._inline ? colorScheme.error : colorScheme.onSurface,
          fontSize: widget._inline ? 13 : 16,
          fontWeight: FontWeight.w600,
        ),
      ),
    ));
    if (widget.subtitle != null) {
      children.add(const SizedBox(height: 6));
      children.add(Text(
        widget.subtitle!,
        textAlign: TextAlign.center,
        style: TextStyle(color: colorScheme.onSurfaceVariant, fontSize: 13),
      ));
    }
    if (_revealed && widget.rawError != null) {
      children.add(const SizedBox(height: 12));
      children.add(SelectableText(
        widget.rawError.toString(),
        textAlign: TextAlign.center,
        style: TextStyle(
          color: colorScheme.onSurfaceVariant,
          fontSize: 11,
          fontFamily: 'monospace',
          height: 1.4,
        ),
      ));
    }

    if (!widget._inline && widget.onRetry != null) {
      children.add(const SizedBox(height: 20));
      children.add(FilledButton.icon(
        onPressed: widget.onRetry,
        icon: const Icon(Icons.refresh, size: 18),
        label: const Text('Try again'),
      ));
    }

    if (widget._inline) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: children,
      );
    }
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: children,
        ),
      ),
    );
  }
}
