import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/friendly_error.dart';
import '../../application/add_lamp_notifier.dart';
import '../../domain/add_lamp_state.dart';

class AddLampPasswordStep extends ConsumerStatefulWidget {
  const AddLampPasswordStep({super.key});

  @override
  ConsumerState<AddLampPasswordStep> createState() =>
      _AddLampPasswordStepState();
}

Future<void> _confirmSkip(BuildContext context, AddLampNotifier notifier) async {
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('Adopt without a password?'),
      content: const Text(
        "Anyone within Bluetooth range will be able to play with this lamp. "
        "You can set a password later from the Setup tab, but it's safer to "
        'pick one now.',
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(ctx).pop(false),
          child: const Text('Pick one'),
        ),
        TextButton(
          onPressed: () => Navigator.of(ctx).pop(true),
          style: TextButton.styleFrom(
            foregroundColor: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
          child: const Text('Skip anyway'),
        ),
      ],
    ),
  );
  if (ok != true) return;
  notifier.setPassword('');
  await notifier.submit();
}

class _AddLampPasswordStepState extends ConsumerState<AddLampPasswordStep> {
  late final TextEditingController _pwd;
  final _confirm = TextEditingController();

  @override
  void initState() {
    super.initState();
    _pwd = TextEditingController(
      text: ref.read(addLampNotifierProvider).password,
    );
  }

  @override
  void dispose() {
    _pwd.dispose();
    _confirm.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(addLampNotifierProvider.notifier);
    final state = ref.watch(addLampNotifierProvider);
    final textTheme = Theme.of(context).textTheme;
    final colorScheme = Theme.of(context).colorScheme;
    final showMismatch =
        _confirm.text.isNotEmpty && _confirm.text != state.password;
    final canContinue = state.password.isNotEmpty &&
        _confirm.text == state.password;
    final name = state.name.isEmpty ? 'your lamp' : state.name;
    return Padding(
      padding: const EdgeInsets.all(AppSpace.xl),
      // SizedBox.expand fills the Padding's width so `crossAxisAlignment
      // .center` lands the heading at screen-center (a bare Column shrinks
      // to its widest child and pins to the left edge of the Padding).
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Text(
              'Set a password',
              textAlign: TextAlign.center,
              style: textTheme.headlineSmall,
            ),
            const SizedBox(height: AppSpace.sm),
            Text(
              "Only phones with this password will be able to control this lamp.",
              textAlign: TextAlign.center,
              style: textTheme.bodyMedium,
            ),
            if (state.error == AddLampError.wrongPassword) ...[
              const SizedBox(height: AppSpace.sm),
              Text(
                "That password didn't match. Try once more.",
                style: textTheme.bodyMedium?.copyWith(color: colorScheme.error),
                textAlign: TextAlign.center,
              ),
            ],
            const SizedBox(height: AppSpace.lg),
            TextField(
              controller: _pwd,
              autofocus: true,
              obscureText: true,
              onChanged: (v) {
                notifier.setPassword(v);
                setState(() {}); // re-evaluate the mismatch banner + button
              },
              decoration: const InputDecoration(
                labelText: 'Password',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: AppSpace.md),
            TextField(
              controller: _confirm,
              obscureText: true,
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                labelText: 'Confirm password',
                border: const OutlineInputBorder(),
                errorText: showMismatch ? "Doesn't match" : null,
              ),
            ),
            const Spacer(),
            if (state.status == AddLampStatus.error &&
                state.error != AddLampError.wrongPassword)
              Padding(
                padding: const EdgeInsets.only(bottom: AppSpace.md),
                child: FriendlyError.inline(
                  title: state.error == AddLampError.connectFailed
                      ? "Your lamp drifted off. Bring your phone closer "
                          'and try again.'
                      : "Adoption didn't go through. Try once more.",
                  rawError: state.errorMessage,
                ),
              ),
            Row(
              children: [
                TextButton(
                  onPressed: state.status == AddLampStatus.working
                      ? null
                      : notifier.previous,
                  child: const Text('Back'),
                ),
                const Spacer(),
                TextButton(
                  onPressed: state.status != AddLampStatus.working
                      ? () => _confirmSkip(context, notifier)
                      : null,
                  style: TextButton.styleFrom(
                      foregroundColor: colorScheme.onSurfaceVariant),
                  child: const Text('Skip'),
                ),
                const SizedBox(width: AppSpace.sm),
                FilledButton(
                  onPressed: (canContinue &&
                          state.status != AddLampStatus.working)
                      ? notifier.submit
                      : null,
                  child: state.status == AddLampStatus.working
                      ? const Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            SizedBox(
                              width: 16,
                              height: 16,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            ),
                            SizedBox(width: AppSpace.sm),
                            Text('Settling in…'),
                          ],
                        )
                      : Text('Take $name home'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
