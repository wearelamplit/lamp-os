import 'package:flutter/material.dart';
import 'lamp_card.dart';
import 'section_header.dart';

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
