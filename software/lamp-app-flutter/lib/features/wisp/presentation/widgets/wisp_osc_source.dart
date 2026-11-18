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
/// no live palette stream. The wisp holds the lamps at Off meanwhile and
/// resumes painting on its own when palettes arrive.
class WaitingForStraylightNotice extends StatelessWidget {
  const WaitingForStraylightNotice({super.key});

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
              'Waiting for Straylight.\n'
              'Lamps stay on their own colours until a palette arrives.',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
        ],
      ),
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
        borderRadius: BorderRadius.circular(AppSpace.sm),
        child: Container(
          padding: const EdgeInsets.symmetric(
            horizontal: AppSpace.md,
            vertical: AppSpace.sm,
          ),
          decoration: BoxDecoration(
            color: colorScheme.surface,
            borderRadius: BorderRadius.circular(AppSpace.sm),
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
