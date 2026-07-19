import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// Single-field numeric editor modal, the tap-to-open counterpart of
/// [RenameDialog]. [onSave] fires with the value clamped to [min]..[max];
/// unparseable input keeps the dialog open.
Future<void> showNumberInputDialog(
  BuildContext context, {
  required String title,
  required String label,
  required int initial,
  required int min,
  required int max,
  required ValueChanged<int> onSave,
}) =>
    showDialog<void>(
      context: context,
      builder: (_) => NumberInputDialog(
        title: title,
        label: label,
        initial: initial,
        min: min,
        max: max,
        onSave: onSave,
      ),
    );

class NumberInputDialog extends StatefulWidget {
  const NumberInputDialog({
    super.key,
    required this.title,
    required this.label,
    required this.initial,
    required this.min,
    required this.max,
    required this.onSave,
  });

  final String title;
  final String label;
  final int initial;
  final int min;
  final int max;
  final ValueChanged<int> onSave;

  @override
  State<NumberInputDialog> createState() => _NumberInputDialogState();
}

class _NumberInputDialogState extends State<NumberInputDialog> {
  late final TextEditingController _ctrl =
      TextEditingController(text: '${widget.initial}');

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  void _save() {
    final n = int.tryParse(_ctrl.text);
    if (n == null) return;
    widget.onSave(n.clamp(widget.min, widget.max));
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(widget.title),
      content: TextField(
        controller: _ctrl,
        autofocus: true,
        keyboardType: TextInputType.number,
        inputFormatters: [FilteringTextInputFormatter.digitsOnly],
        decoration: InputDecoration(
          labelText: widget.label,
          helperText: '${widget.min}–${widget.max}',
        ),
        onSubmitted: (_) => _save(),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: _save,
          child: const Text('Save'),
        ),
      ],
    );
  }
}
