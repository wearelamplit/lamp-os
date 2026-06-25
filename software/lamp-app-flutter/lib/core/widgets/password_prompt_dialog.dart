import 'package:flutter/material.dart';

import '../theme/brand_colors.dart';
import 'inactive_backdrop_scrim.dart';

/// Generic password-prompt dialog. Returns the entered password on
/// confirm, or `null` if the user cancels (or pops the dialog).
///
/// Used by feature surfaces that need to collect a password without the
/// caller having to roll its own obscured TextField + show/hide eye
/// button + submit-on-enter wiring every time. Pure UI — the caller owns
/// what to do with the result (e.g. ship it as a wispOp arg).
///
/// Distinct from [ConnectPasswordPrompt] which is BLE-auth-specific and
/// owns its own busy/error state tied to controlNotifier. This dialog is
/// stateless from the caller's POV: it just asks for a string.
Future<String?> showPasswordPromptDialog(
  BuildContext context, {
  required String title,
  String? subtitle,
  String? initialValue,
  String confirmLabel = 'Save',
  String cancelLabel = 'Cancel',
}) {
  return showBlurredDialog<String>(
    context: context,
    barrierDismissible: true,
    builder: (ctx) => _PasswordPromptDialog(
      title: title,
      subtitle: subtitle,
      initialValue: initialValue,
      confirmLabel: confirmLabel,
      cancelLabel: cancelLabel,
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
  });

  final String title;
  final String? subtitle;
  final String? initialValue;
  final String confirmLabel;
  final String cancelLabel;

  @override
  State<_PasswordPromptDialog> createState() => _PasswordPromptDialogState();
}

class _PasswordPromptDialogState extends State<_PasswordPromptDialog> {
  late final TextEditingController _ctrl =
      TextEditingController(text: widget.initialValue ?? '');
  bool _obscured = true;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  void _confirm() {
    final value = _ctrl.text;
    if (value.isEmpty) return;
    Navigator.of(context).pop(value);
  }

  void _cancel() => Navigator.of(context).pop();

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: Text(
        widget.title,
        style: const TextStyle(color: BrandColors.lampWhite),
      ),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (widget.subtitle != null) ...[
            Text(
              widget.subtitle!,
              style: const TextStyle(
                color: BrandColors.fogGrey,
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
            onSubmitted: (_) => _confirm(),
            decoration: InputDecoration(
              labelText: 'Password',
              labelStyle: const TextStyle(color: BrandColors.fogGrey),
              suffixIcon: IconButton(
                icon: Icon(
                  _obscured ? Icons.visibility : Icons.visibility_off,
                  color: BrandColors.fogGrey,
                ),
                onPressed: () => setState(() => _obscured = !_obscured),
              ),
            ),
            style: const TextStyle(color: BrandColors.lampWhite),
          ),
        ],
      ),
      actions: [
        TextButton(
          key: const Key('password-prompt-cancel'),
          onPressed: _cancel,
          child: Text(widget.cancelLabel),
        ),
        FilledButton(
          key: const Key('password-prompt-confirm'),
          onPressed: _confirm,
          child: Text(widget.confirmLabel),
        ),
      ],
    );
  }
}
