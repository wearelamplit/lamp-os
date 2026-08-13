import 'package:flutter/material.dart';

/// Single-field rename modal. Keeps a tappable row editable without a full
/// sub-screen. Hosted by a `StatefulWidget` so its `TextEditingController`
/// has a lifecycle to dispose on (a plain `showDialog` closure leaks it).
///
/// [onSave] fires with the trimmed value on Save; an empty value is passed
/// through, callers that reject empty names guard on their side.
Future<void> showRenameDialog(
  BuildContext context, {
  required String title,
  required String label,
  required String initial,
  required ValueChanged<String> onSave,
  int? maxLength,
  String? hintText,
}) =>
    showDialog<void>(
      context: context,
      builder: (_) => RenameDialog(
        title: title,
        label: label,
        initial: initial,
        onSave: onSave,
        maxLength: maxLength,
        hintText: hintText,
      ),
    );

class RenameDialog extends StatefulWidget {
  const RenameDialog({
    super.key,
    required this.title,
    required this.label,
    required this.initial,
    required this.onSave,
    this.maxLength,
    this.hintText,
  });

  final String title;
  final String label;
  final String initial;
  final ValueChanged<String> onSave;
  final int? maxLength;
  final String? hintText;

  @override
  State<RenameDialog> createState() => _RenameDialogState();
}

class _RenameDialogState extends State<RenameDialog> {
  late final TextEditingController _ctrl =
      TextEditingController(text: widget.initial);

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(widget.title),
      content: TextField(
        controller: _ctrl,
        autofocus: true,
        maxLength: widget.maxLength,
        decoration: InputDecoration(
          labelText: widget.label,
          hintText: widget.hintText,
          counterText: '',
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () {
            widget.onSave(_ctrl.text.trim());
            Navigator.of(context).pop();
          },
          child: const Text('Save'),
        ),
      ],
    );
  }
}
