import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/widgets/password_prompt_dialog.dart';
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
    final result = await showPasswordPromptDialog(
      context,
      title: 'Enter password',
      confirmLabel: 'Connect',
      barrierDismissible: false,
      onSubmit: (pw) async {
        try {
          await ref
              .read(controlNotifierProvider(widget.lampId).notifier)
              .submitConnectPassword(pw);
          return null; // success — notifier triggers a rebuild
        } on LampAuthRequiredException {
          return ('Wrong password. Try again.', null);
        } catch (e) {
          return (
            "Your lamp didn't answer. Bring your phone closer and try again.",
            e,
          );
        }
      },
    );
    _dialogOpen = false;
    // null → user cancelled; pop back to the lamps list
    if (!mounted) return;
    if (result == null) {
      final navigator = Navigator.of(context);
      if (navigator.canPop()) navigator.pop();
    }
  }

  @override
  Widget build(BuildContext context) {
    return ConnectingView(deviceId: widget.lampId);
  }
}
