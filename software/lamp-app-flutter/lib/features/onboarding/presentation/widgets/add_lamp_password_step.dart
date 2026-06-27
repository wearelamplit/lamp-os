import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
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
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Adopt without a password?',
          style: TextStyle(color: BrandColors.lampWhite)),
      content: const Text(
        "Anyone within Bluetooth range will be able to play with this lamp. "
        "You can set a password later from the Setup tab — but it's safer to "
        'pick one now.',
        style: TextStyle(color: BrandColors.fogGrey),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(ctx).pop(false),
          child: const Text('Pick one'),
        ),
        FilledButton(
          style: FilledButton.styleFrom(
              backgroundColor: BrandColors.slateGrey),
          onPressed: () => Navigator.of(ctx).pop(true),
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
  late final TextEditingController _pwd = TextEditingController(
    text: ref.read(addLampNotifierProvider).password,
  );
  final _confirm = TextEditingController();

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
    final showMismatch =
        _confirm.text.isNotEmpty && _confirm.text != state.password;
    final canContinue = state.password.isNotEmpty &&
        _confirm.text == state.password;
    final isVerifying = state.step == AddLampStep.verifying;
    return Padding(
      padding: const EdgeInsets.all(24),
      // SizedBox.expand fills the Padding's width so `crossAxisAlignment
      // .center` lands the heading at screen-center (a bare Column shrinks
      // to its widest child and pins to the left edge of the Padding).
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
          const Text(
            'Set a password',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: BrandColors.lampWhite,
              fontSize: 18,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 8),
          const Text(
            "Only phones with this password will be able to control this lamp.",
            textAlign: TextAlign.center,
            style: TextStyle(color: BrandColors.fogGrey),
          ),
          if (state.error == AddLampError.wrongPassword) ...[
            const SizedBox(height: 8),
            const Text(
              "That password didn't match — try once more.",
              style: TextStyle(color: BrandColors.error),
              textAlign: TextAlign.center,
            ),
          ],
          const SizedBox(height: 16),
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
          const SizedBox(height: 12),
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
          if (isVerifying) ...[
            const _VerifyingTips(),
            const SizedBox(height: 16),
          ],
          if (state.status == AddLampStatus.error &&
              state.error != AddLampError.wrongPassword)
            Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: FriendlyError.inline(
                title: state.error == AddLampError.connectFailed
                    ? "Your lamp drifted off — bring your phone closer "
                        'and try again.'
                    : "Adoption didn't go through — try once more.",
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
                onPressed: (state.status != AddLampStatus.working &&
                        !isVerifying)
                    ? () => _confirmSkip(context, notifier)
                    : null,
                style: TextButton.styleFrom(
                    foregroundColor: BrandColors.slateGrey),
                child: const Text('Skip'),
              ),
              const SizedBox(width: 8),
              FilledButton(
                onPressed: (canContinue &&
                        state.status != AddLampStatus.working &&
                        !isVerifying)
                    ? notifier.submit
                    : null,
                child: (state.status == AddLampStatus.working || isVerifying)
                    ? const Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          ),
                          SizedBox(width: 8),
                          Text('Settling in…'),
                        ],
                      )
                    : const Text('Welcome them home'),
              ),
            ],
          ),
          ],
        ),
      ),
    );
  }
}

/// Rotates through whimsical "what your lamp is doing right now" tips during
/// the 8-second post-claim verify window. Replaces the dead-air spinner with
/// something readable. Cancels its timer on dispose so it doesn't leak past
/// the verifying step.
class _VerifyingTips extends StatefulWidget {
  const _VerifyingTips();

  @override
  State<_VerifyingTips> createState() => _VerifyingTipsState();
}

class _VerifyingTipsState extends State<_VerifyingTips> {
  static const _tips = [
    'Saving your password…',
    'Picking out a critter friend…',
    'Stretching after the long sleep…',
    'Almost settled in…',
  ];
  int _i = 0;
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(milliseconds: 2200), (_) {
      if (!mounted) return;
      setState(() => _i = (_i + 1) % _tips.length);
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedSwitcher(
      duration: const Duration(milliseconds: 350),
      child: Padding(
        key: ValueKey(_i),
        padding: const EdgeInsets.symmetric(horizontal: 16),
        child: Text(
          _tips[_i],
          textAlign: TextAlign.center,
          style: const TextStyle(
            color: BrandColors.fogGrey,
            fontSize: 13,
            letterSpacing: 0.3,
            height: 1.4,
          ),
        ),
      ),
    );
  }
}
