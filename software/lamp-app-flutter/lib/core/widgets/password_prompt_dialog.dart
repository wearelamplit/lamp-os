import 'package:flutter/material.dart';

import 'friendly_error.dart';

/// Generic password-prompt dialog. Returns the entered password on
/// confirm, or `null` if the user cancels (or pops the dialog).
///
/// Used by feature surfaces that need to collect a password without the
/// caller having to roll its own obscured TextField + show/hide eye
/// button + submit-on-enter wiring every time. Pure UI — the caller owns
/// what to do with the result (e.g. ship it as a wispOp arg).
///
/// When [onSubmit] is provided the dialog drives an async operation:
/// the callback receives the typed password and should return `null` on
/// success (dialog pops, returning the password to the caller) or a
/// `(errorMessage, rawError)` record to show an inline error and keep
/// the dialog open. Buttons and field are disabled while the call is
/// in flight.
Future<String?> showPasswordPromptDialog(
  BuildContext context, {
  required String title,
  String? subtitle,
  String? initialValue,
  String confirmLabel = 'Save',
  String cancelLabel = 'Cancel',
  bool barrierDismissible = true,
  /// Called on submit. Return null on success (dialog pops).
  /// Return `(errorMessage, rawError)` to show an inline error and
  /// keep the dialog open. rawError may be null.
  Future<(String, Object?)?> Function(String pw)? onSubmit,
}) {
  return showDialog<String>(
    context: context,
    barrierDismissible: barrierDismissible,
    builder: (ctx) => _PasswordPromptDialog(
      title: title,
      subtitle: subtitle,
      initialValue: initialValue,
      confirmLabel: confirmLabel,
      cancelLabel: cancelLabel,
      onSubmit: onSubmit,
    ),
  );
}

class _PasswordPromptDialog extends StatefulWidget {
  const _PasswordPromptDialog({
    required this.title,
    this.subtitle,
    this.initialValue,
    required this.confirmLabel,
    required this.cancelLabel,
    this.onSubmit,
  });

  final String title;
  final String? subtitle;
  final String? initialValue;
  final String confirmLabel;
  final String cancelLabel;
  final Future<(String, Object?)?> Function(String pw)? onSubmit;

  @override
  State<_PasswordPromptDialog> createState() => _PasswordPromptDialogState();
}

class _PasswordPromptDialogState extends State<_PasswordPromptDialog> {
  late final TextEditingController _ctrl =
      TextEditingController(text: widget.initialValue ?? '');
  bool _obscured = true;
  bool _busy = false;
  String? _errorMsg;
  Object? _rawError;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  Future<void> _confirm() async {
    final value = _ctrl.text;
    if (value.isEmpty || _busy) return;
    if (widget.onSubmit == null) {
      Navigator.of(context).pop(value);
      return;
    }
    setState(() {
      _busy = true;
      _errorMsg = null;
      _rawError = null;
    });
    final result = await widget.onSubmit!(value);
    if (!mounted) return;
    if (result == null) {
      Navigator.of(context).pop(value);
    } else {
      setState(() {
        _busy = false;
        _errorMsg = result.$1;
        _rawError = result.$2;
      });
    }
  }

  void _cancel() => Navigator.of(context).pop();

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return AlertDialog(
      backgroundColor: colorScheme.surface,
      title: Text(
        widget.title,
        style: TextStyle(color: colorScheme.onSurface),
      ),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (widget.subtitle != null) ...[
            Text(
              widget.subtitle!,
              style: TextStyle(
                color: colorScheme.onSurfaceVariant,
                fontSize: 13,
              ),
            ),
            const SizedBox(height: 12),
          ],
          TextField(
            controller: _ctrl,
            key: const Key('password-prompt-field'),
            autofocus: true,
            obscureText: _obscured,
            autocorrect: false,
            enableSuggestions: false,
            enabled: !_busy,
            onSubmitted: (_) => _confirm(),
            decoration: InputDecoration(
              labelText: 'Password',
              labelStyle: TextStyle(color: colorScheme.onSurfaceVariant),
              suffixIcon: IconButton(
                icon: Icon(
                  _obscured ? Icons.visibility : Icons.visibility_off,
                  color: colorScheme.onSurfaceVariant,
                ),
                onPressed: () => setState(() => _obscured = !_obscured),
              ),
            ),
            style: TextStyle(color: colorScheme.onSurface),
          ),
          if (_errorMsg != null) ...[
            const SizedBox(height: 12),
            FriendlyError.inline(
              title: _errorMsg!,
              rawError: _rawError,
            ),
          ],
        ],
      ),
      actions: [
        TextButton(
          key: const Key('password-prompt-cancel'),
          onPressed: _busy ? null : _cancel,
          child: Text(widget.cancelLabel),
        ),
        FilledButton(
          key: const Key('password-prompt-confirm'),
          onPressed: _busy ? null : _confirm,
          child: Text(widget.confirmLabel),
        ),
      ],
    );
  }
}
