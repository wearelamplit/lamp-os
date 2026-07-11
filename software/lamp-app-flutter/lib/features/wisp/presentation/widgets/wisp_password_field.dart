import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/widgets/app_snackbar.dart';
import '../../../../core/widgets/settings_row.dart';
import '../../application/wisp_notifier.dart';

/// Password row for the wisp Settings pane. Tapping opens a dialog to
/// set, change, or clear the control password. Driven off
/// [WispStatus.hasPassword]: shows "Change" / "Clear" when one is set,
/// "Set password" when factory-fresh.
class WispPasswordField extends ConsumerWidget {
  const WispPasswordField({super.key, required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final status = ref.watch(wispNotifierProvider(lampId)).value;
    final hasPassword = status?.hasPassword ?? false;
    final wispMac = status?.wispMac;

    return SettingsRow(
      icon: Icons.lock_outline,
      title: hasPassword ? 'Password set' : 'No password',
      subtitle: hasPassword ? 'Tap to change or clear' : 'Tap to set',
      drillChevron: true,
      onTap: wispMac == null
          ? null
          : () => _showPasswordDialog(context, ref, hasPassword, wispMac),
    );
  }

  Future<void> _showPasswordDialog(
    BuildContext context,
    WidgetRef ref,
    bool hasPassword,
    String wispMac,
  ) async {
    await showDialog<void>(
      context: context,
      builder: (ctx) => _WispPasswordDialog(
        lampId: lampId,
        hasPassword: hasPassword,
        wispMac: wispMac,
      ),
    );
  }
}

class _WispPasswordDialog extends ConsumerStatefulWidget {
  const _WispPasswordDialog({
    required this.lampId,
    required this.hasPassword,
    required this.wispMac,
  });

  final String lampId;
  final bool hasPassword;
  final String wispMac;

  @override
  ConsumerState<_WispPasswordDialog> createState() =>
      _WispPasswordDialogState();
}

class _WispPasswordDialogState extends ConsumerState<_WispPasswordDialog> {
  final _ctrl = TextEditingController();
  bool _loading = false;
  String? _currentPasswordError;

  @override
  void dispose() {
    _ctrl.dispose();
    _currentCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(widget.hasPassword ? 'Change Password' : 'Set Password'),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (widget.hasPassword) ...[
            Text(
              'Enter your current password first, then the new one. '
              'Or clear to remove it.',
              style: Theme.of(context).textTheme.bodySmall,
            ),
            const SizedBox(height: 12),
            TextField(
              decoration: InputDecoration(
                labelText: 'Current password',
                errorText: _currentPasswordError,
              ),
              obscureText: true,
              onChanged: (_) => setState(() => _currentPasswordError = null),
              controller: _currentCtrl,
              autofocus: true,
            ),
            const SizedBox(height: 8),
          ],
          TextField(
            controller: _ctrl,
            decoration: const InputDecoration(labelText: 'New password'),
            obscureText: true,
            autofocus: !widget.hasPassword,
          ),
        ],
      ),
      actions: [
        if (widget.hasPassword)
          TextButton(
            onPressed: _loading ? null : _doClear,
            child: const Text('Clear'),
          ),
        TextButton(
          onPressed: _loading ? null : () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        TextButton(
          onPressed: _loading ? null : _doSet,
          child: const Text('Save'),
        ),
      ],
    );
  }

  final _currentCtrl = TextEditingController();

  Future<void> _doSet() async {
    final newPw = _ctrl.text.trim();
    if (newPw.isEmpty) return;
    if (widget.hasPassword) {
      final current = _currentCtrl.text.trim();
      if (current.isEmpty) {
        setState(() => _currentPasswordError = 'Enter the current password');
        return;
      }
    }
    final n = ref.read(wispNotifierProvider(widget.lampId).notifier);
    setState(() => _loading = true);
    try {
      if (widget.hasPassword) {
        final current = _currentCtrl.text.trim();
        await n.cachePassword(widget.wispMac, current);
      }
      await n.setPassword(newPw);
      if (mounted) Navigator.of(context).pop();
    } catch (_) {
      if (mounted) {
        setState(() => _loading = false);
        AppSnackbar.error(context, "Couldn't set password. Try again.");
      }
    }
  }

  Future<void> _doClear() async {
    final current = _currentCtrl.text.trim();
    if (current.isEmpty) {
      setState(() => _currentPasswordError = 'Enter the current password');
      return;
    }
    final n = ref.read(wispNotifierProvider(widget.lampId).notifier);
    setState(() => _loading = true);
    try {
      await n.cachePassword(widget.wispMac, current);
      await n.clearPassword();
      if (mounted) Navigator.of(context).pop();
    } catch (_) {
      if (mounted) {
        setState(() => _loading = false);
        AppSnackbar.error(context, "Couldn't clear password. Try again.");
      }
    }
  }
}
