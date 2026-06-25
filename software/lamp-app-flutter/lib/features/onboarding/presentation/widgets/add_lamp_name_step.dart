// lib/features/onboarding/presentation/widgets/add_lamp_name_step.dart
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../application/add_lamp_notifier.dart';

class AddLampNameStep extends ConsumerStatefulWidget {
  const AddLampNameStep({super.key});

  @override
  ConsumerState<AddLampNameStep> createState() => _AddLampNameStepState();
}

class _AddLampNameStepState extends ConsumerState<AddLampNameStep> {
  late final TextEditingController _controller =
      TextEditingController(text: ref.read(addLampNotifierProvider).name);

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(addLampNotifierProvider.notifier);
    final state = ref.watch(addLampNotifierProvider);
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
            'What will you call them?',
            textAlign: TextAlign.center,
            style: TextStyle(
              color: BrandColors.lampWhite,
              fontSize: 18,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 6),
          const Text(
            "Every lamp deserves a name. They'll wear it proudly.",
            textAlign: TextAlign.center,
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 13),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: _controller,
            autofocus: true,
            onChanged: notifier.setName,
            decoration: const InputDecoration(
              labelText: 'Their name',
              border: OutlineInputBorder(),
            ),
          ),
          const Spacer(),
          Row(
            children: [
              TextButton(
                onPressed: notifier.previous,
                child: const Text('Back'),
              ),
              const Spacer(),
              FilledButton(
                onPressed: state.name.isEmpty ? null : notifier.next,
                child: const Text('Continue'),
              ),
            ],
          ),
          ],
        ),
      ),
    );
  }
}
