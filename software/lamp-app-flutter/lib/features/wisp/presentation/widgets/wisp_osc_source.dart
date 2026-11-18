import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_snackbar.dart';
import '../../../../core/widgets/section_header.dart';
import '../../domain/wisp_config.dart';
import '../../domain/wisp_source_mode.dart';
import '../../domain/wisp_status.dart';
import 'wisp_settings_pending.dart';

/// Inline notice shown under the OSC source pill while the wisp has
/// no live palette stream from [sender]. The wisp holds the lamps at Off
/// meanwhile and resumes painting on its own when palettes arrive.
class WaitingForOscNotice extends StatelessWidget {
  const WaitingForOscNotice({super.key, required this.sender});

  final String sender;

  @override
  Widget build(BuildContext context) {
    final secondary = Theme.of(context).colorScheme.secondary;
    return Container(
      padding: const EdgeInsets.all(AppSpace.md),
      decoration: BoxDecoration(
        color: secondary.withValues(alpha: 0.10),
        border: Border.all(
          color: secondary.withValues(alpha: 0.40),
        ),
        borderRadius: BorderRadius.circular(AppSpace.sm),
      ),
      child: Row(
        children: [
          Icon(
            Icons.graphic_eq,
            size: 16, // deliberate dimension, not spacing
            color: secondary,
          ),
          const SizedBox(width: AppSpace.sm),
          Expanded(
            child: Text(
              'Waiting for $sender.\n'
              'Lamps stay on their own colours until a palette arrives.',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
        ],
      ),
    );
  }
}

/// Picks which OSC sender the wisp listens to. [current] is
/// [OscMode.unknown] for a sender this app cannot name: nothing is selected
/// and a line asks for an app update.
class OscModeSwitch extends StatelessWidget {
  const OscModeSwitch({
    super.key,
    required this.current,
    required this.onSelect,
  });

  final OscMode current;
  final ValueChanged<OscMode> onSelect;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SectionHeader('OSC sender'),
        const SizedBox(height: AppSpace.sm),
        SegmentedButton<OscMode>(
          key: const Key('wisp-osc-mode'),
          style: SegmentedButton.styleFrom(
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
          ),
          segments: const [
            ButtonSegment(
              value: OscMode.straylight,
              label: Text('Straylight'),
            ),
            ButtonSegment(value: OscMode.maestro, label: Text('Maestro')),
          ],
          selected: {if (current != OscMode.unknown) current},
          emptySelectionAllowed: current == OscMode.unknown,
          showSelectedIcon: false,
          onSelectionChanged: (set) {
            if (set.isEmpty) return;
            onSelect(set.first);
          },
        ),
        if (current == OscMode.unknown) ...[
          const SizedBox(height: AppSpace.sm),
          Text(
            "This wisp listens to an OSC sender this app doesn't know. "
            'Update the app to see it.',
            key: const Key('wisp-osc-mode-unknown'),
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ],
    );
  }
}

/// Picks the Maestro palette slot the wisp reads, and says what to set on
/// the Maestro to reach it at [address] (the wisp's send-to address, empty
/// while it has none).
class MaestroSlotPicker extends StatelessWidget {
  const MaestroSlotPicker({
    super.key,
    required this.current,
    required this.address,
    required this.onSelect,
  });

  final int? current;
  final String address;
  final ValueChanged<int> onSelect;

  @override
  Widget build(BuildContext context) {
    final slot = current;
    final target = address.isEmpty ? "the wisp's address" : address;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SectionHeader('Maestro palette slot'),
        const SizedBox(height: AppSpace.sm),
        InputDecorator(
          decoration: InputDecoration(
            contentPadding: const EdgeInsets.symmetric(
              horizontal: AppSpace.md,
            ),
            border: OutlineInputBorder(
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
          ),
          child: DropdownButtonHideUnderline(
            child: DropdownButton<int>(
              key: const Key('wisp-maestro-slot'),
              isExpanded: true,
              value: slot,
              items: [
                for (var n = kMaestroSlotMin; n <= kMaestroSlotMax; n++)
                  DropdownMenuItem(value: n, child: Text('Slot $n')),
              ],
              onChanged: (n) {
                if (n != null && n != slot) onSelect(n);
              },
            ),
          ),
        ),
        if (slot != null) ...[
          const SizedBox(height: AppSpace.sm),
          Text(
            'On the Maestro, send palette slot $slot to $target, '
            'port ${WispConfig.kOscPort}.',
            key: const Key('wisp-maestro-help'),
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ],
    );
  }
}

/// Picks which Straylight palette role the wisp follows.
class OscRolePicker extends StatelessWidget {
  const OscRolePicker({
    super.key,
    required this.current,
    required this.onSelect,
  });

  final OscRole current;
  final ValueChanged<OscRole> onSelect;

  static String _label(OscRole r) {
    switch (r) {
      case OscRole.primary:
        return 'Primary';
      case OscRole.secondary:
        return 'Secondary';
      case OscRole.tertiary:
        return 'Tertiary';
      case OscRole.fill:
        return 'Fill';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Straylight palette'),
        const SizedBox(height: AppSpace.sm),
        SegmentedButton<OscRole>(
          key: const Key('wisp-osc-role-picker'),
          style: SegmentedButton.styleFrom(
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
          ),
          segments: [
            for (final r in OscRole.values)
              ButtonSegment(value: r, label: Text(_label(r))),
          ],
          selected: {current},
          showSelectedIcon: false,
          onSelectionChanged: (set) {
            if (set.isEmpty) return;
            onSelect(set.first);
          },
        ),
      ],
    );
  }
}

/// Where an OSC sender should send, from the wisp's own reports, with a copy
/// button. Calls [onLinkDisagrees] for each new status whose link state
/// contradicts the held address, so the caller can ask for the config again.
class OscSendTo extends StatefulWidget {
  const OscSendTo({
    super.key,
    required this.lampId,
    required this.status,
    required this.onLinkDisagrees,
  });

  final String lampId;
  final WispStatus status;
  final VoidCallback onLinkDisagrees;

  @override
  State<OscSendTo> createState() => _OscSendToState();
}

class _OscSendToState extends State<OscSendTo> {
  @override
  void initState() {
    super.initState();
    if (widget.status.ipDisagreesWithLink) widget.onLinkDisagrees();
  }

  @override
  void didUpdateWidget(OscSendTo old) {
    super.didUpdateWidget(old);
    if (widget.status != old.status && widget.status.ipDisagreesWithLink) {
      widget.onLinkDisagrees();
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = widget.status;
    final address = status.oscSendTo;
    final Widget body;
    if (address.isNotEmpty) {
      body = _AddressBox(address: address);
    } else if (!status.configKnown) {
      body = WispSettingsPending(lampId: widget.lampId);
    } else if (status.ip != null) {
      body = Text(
        'No address yet. The wisp joins Wi-Fi while OSC is on (set it in '
        'Settings), or plug in Ethernet.',
        key: const Key('wisp-osc-no-address'),
        style: Theme.of(context).textTheme.bodySmall,
      );
    } else {
      return const SizedBox.shrink();
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SectionHeader('Send OSC to'),
        const SizedBox(height: AppSpace.sm),
        body,
      ],
    );
  }
}

class _AddressBox extends StatelessWidget {
  const _AddressBox({required this.address});

  final String address;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return Semantics(
      button: true,
      label: 'Copy address $address',
      excludeSemantics: true,
      child: InkWell(
        key: const Key('wisp-osc-send-to'),
        onTap: () async {
          await Clipboard.setData(ClipboardData(text: address));
          if (context.mounted) AppSnackbar.info(context, 'Copied $address');
        },
        borderRadius: BorderRadius.circular(AppRadius.card),
        child: Container(
          padding: const EdgeInsets.symmetric(
            horizontal: AppSpace.md,
            vertical: AppSpace.sm,
          ),
          decoration: BoxDecoration(
            color: colorScheme.surface,
            borderRadius: BorderRadius.circular(AppRadius.card),
            border: Border.all(
              color: colorScheme.primary.withValues(alpha: 0.5),
            ),
          ),
          child: Row(
            children: [
              Text(
                address,
                style: TextStyle(
                  color: colorScheme.primary,
                  fontSize: 15,
                  fontFamily: 'monospace',
                ),
              ),
              const SizedBox(width: AppSpace.md),
              Expanded(
                child: Text(
                  'port ${WispConfig.kOscPort}',
                  style: textTheme.bodySmall,
                ),
              ),
              Icon(
                Icons.copy,
                color: colorScheme.onSurfaceVariant,
                size: 16, // deliberate dimension, not spacing
              ),
            ],
          ),
        ),
      ),
    );
  }
}
