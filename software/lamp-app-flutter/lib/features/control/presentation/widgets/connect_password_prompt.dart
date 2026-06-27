import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../../../core/widgets/friendly_error.dart';
import '../../application/control_notifier.dart';
import '../../application/lamp_auth_required_exception.dart';
import 'connecting_view.dart';

/// Surface shown when [controlNotifierProvider] enters an AsyncError carrying
/// [LampAuthRequiredException] — typically because the password stored in
/// inventory no longer satisfies the firmware's auth gate (changed on another
/// device, second-device install, etc.). Renders [ConnectingView] underneath
/// and pops an [AlertDialog] over it for the user to enter the password.
class ConnectPasswordPrompt extends ConsumerStatefulWidget {
  const ConnectPasswordPrompt({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<ConnectPasswordPrompt> createState() =>
      _ConnectPasswordPromptState();
}

class _ConnectPasswordPromptState
    extends ConsumerState<ConnectPasswordPrompt> {
  bool _dialogOpen = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted || _dialogOpen) return;
      _dialogOpen = true;
      _showPasswordDialog();
    });
  }

  Future<void> _showPasswordDialog() async {
    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (dialogContext) => _PasswordDialog(lampId: widget.lampId),
    );
    _dialogOpen = false;
  }

  @override
  Widget build(BuildContext context) {
    return ConnectingView(deviceId: widget.lampId);
  }
}

class _PasswordDialog extends ConsumerStatefulWidget {
  const _PasswordDialog({required this.lampId});
  final String lampId;

  @override
  ConsumerState<_PasswordDialog> createState() => _PasswordDialogState();
}

class _PasswordDialogState extends ConsumerState<_PasswordDialog> {
  final _ctrl = TextEditingController();
  bool _obscured = true;
  bool _busy = false;
  String? _error;
  Object? _rawError;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    final pw = _ctrl.text;
    if (pw.isEmpty || _busy) return;
    setState(() {
      _busy = true;
      _error = null;
      _rawError = null;
    });
    try {
      await ref
          .read(controlNotifierProvider(widget.lampId).notifier)
          .submitConnectPassword(pw);
      // Success — invalidateSelf in the notifier triggers a rebuild. Pop the
      // dialog so the underlying control surface (now AsyncData) renders.
      if (mounted) Navigator.of(context).pop();
    } on LampAuthRequiredException {
      if (!mounted) return;
      setState(() {
        _busy = false;
        _error = 'Wrong password — try again.';
        _rawError = null;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _busy = false;
        _error = "Your lamp didn't answer. Bring your phone closer "
            'and try again.';
        _rawError = e;
      });
    }
  }

  void _cancel() {
    Navigator.of(context).pop(); // close dialog
    final navigator = Navigator.of(context);
    if (navigator.canPop()) navigator.pop(); // back out to the lamps list
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Enter password',
          style: TextStyle(color: BrandColors.lampWhite)),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          TextField(
            controller: _ctrl,
            autofocus: true,
            obscureText: _obscured,
            enabled: !_busy,
            onSubmitted: (_) => _submit(),
            decoration: InputDecoration(
              labelText: 'Password',
              suffixIcon: IconButton(
                icon: Icon(
                    _obscured ? Icons.visibility : Icons.visibility_off),
                onPressed: () => setState(() => _obscured = !_obscured),
              ),
            ),
            style: const TextStyle(color: BrandColors.lampWhite),
          ),
          if (_error != null) ...[
            const SizedBox(height: 12),
            FriendlyError.inline(
              title: _error!,
              rawError: _rawError,
            ),
          ],
        ],
      ),
      actions: [
        TextButton(
          onPressed: _busy ? null : _cancel,
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: _busy ? null : _submit,
          child: const Text('Connect'),
        ),
      ],
    );
  }
}
