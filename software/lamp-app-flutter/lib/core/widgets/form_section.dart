import 'package:flutter/material.dart';
import 'lamp_card.dart';
import 'section_header.dart';

/// Groups mixed-type controls that edit one config object (e.g. count field +
/// segmented button + nav row for one LED section). For homogeneous tap-rows
/// use [SettingsRow] under [SettingsGroupHeading]; for a single control or
/// full-bleed selector use loose [SectionHeader] + tokenized spacing.
/// See `docs/FORM_STYLING.md` for the full grouping idiom and spacing rules.
class FormSection extends StatelessWidget {
  const FormSection({required this.title, required this.children, super.key});

  final String title;
  final List<Widget> children;

  List<Widget> _withDividers(BuildContext context, List<Widget> items) {
    final divider = Divider(
      height: 1,
      thickness: 1,
      color: Theme.of(context).colorScheme.outlineVariant,
    );
    return [
      for (int i = 0; i < items.length; i++) ...[
        if (i > 0) divider,
        items[i],
      ],
    ];
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SectionHeader(title),
        LampCard(
          padding: EdgeInsets.zero,
          child: Column(children: _withDividers(context, children)),
        ),
      ],
    );
  }
}
