// Compact panel surfacing the firmware update lifecycle on a single
// lamp. Lives on the Info tab; renders as an InfoPanel-styled card.
//
// Visual states (mirror of FirmwareState's sealed subclasses):
//   Idle                → "Check for updates" button
//   Downloading         → spinner + "Checking…"
//   Verifying           → spinner + "Verifying signature…"
//   ReadyToInstall      → version line + "Install" / "Cancel"
//   OfferSent           → spinner + "Waiting for lamp…"
//   Streaming           → progress bar + percent + "Cancel"
//   Finalizing          → spinner + "Finishing up…"
//   Succeeded           → checkmark + "Update installed. Lamp is rebooting."
//   Failed              → reason + "Try again"

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/theme/brand_extras.dart';
import '../../../core/widgets/info_panel.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../application/firmware_notifier.dart';
import '../data/firmware_release_client.dart';
import '../domain/firmware_state.dart';

// v0x04 channel strings carry `{lampType}-{channel}` — strip the prefix
// when rendering so the user sees `stable` / `beta` / `dev`, not
// `standard-stable`. The lamp variant is internal routing metadata.
String _displayChannel(String raw) {
  final dash = raw.indexOf('-');
  return dash >= 0 ? raw.substring(dash + 1) : raw;
}

class FirmwareUpdatePanel extends ConsumerWidget {
  const FirmwareUpdatePanel({
    super.key,
    required this.deviceId,
    required this.lampType,
    this.channel = FirmwareChannel.stable,
  });

  final String deviceId;
  final String? lampType;
  final FirmwareChannel channel;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(firmwareNotifierProvider(deviceId));
    final notifier = ref.read(firmwareNotifierProvider(deviceId).notifier);

    // Fall back to inventory's persisted value when the live LampSection
    // hasn't reported lampType yet (pre-section-read window, or a lamp
    // whose firmware is too old to emit the field). Without this, the
    // persisted lampType is write-only and the firmware-fetch path errors
    // out unnecessarily.
    String? resolvedLampType = lampType;
    if (resolvedLampType == null) {
      final inv = ref.watch(inventoryNotifierProvider).value;
      if (inv != null) {
        for (final entry in inv) {
          if (entry.id == deviceId) {
            resolvedLampType = entry.lampType;
            break;
          }
        }
      }
    }

    return InfoPanel(
      child: switch (state) {
        FirmwareIdle() => _IdleView(
            onCheck: () => notifier.checkForUpdate(
              channel: channel,
              lampType: resolvedLampType,
            ),
          ),
        FirmwareDownloading() => const _BusyView(label: 'Checking for updates…'),
        FirmwareVerifying() => const _BusyView(label: 'Verifying signature…'),
        FirmwareReadyToInstall(:final footer, :final imageBytes) => _ReadyView(
            versionString: footer.versionString,
            channel: _displayChannel(footer.channel),
            imageBytes: imageBytes,
            onInstall: notifier.install,
            onCancel: notifier.reset,
          ),
        FirmwareOfferSent() => const _BusyView(label: 'Waiting for the lamp to accept…'),
        FirmwareStreaming(
          :final chunksSent,
          :final totalChunks,
          :final progress,
        ) => _StreamingView(
            chunksSent: chunksSent,
            totalChunks: totalChunks,
            progress: progress,
            onCancel: notifier.cancel,
          ),
        FirmwareFinalizing() => const _BusyView(label: 'Finishing up…'),
        FirmwareSucceeded(:final footer) => _SucceededView(
            versionString: footer.versionString,
            onDismiss: notifier.reset,
          ),
        FirmwareFailed(:final reason) => _FailedView(
            reason: reason,
            onRetry: notifier.reset,
          ),
      },
    );
  }
}

class _IdleView extends StatelessWidget {
  const _IdleView({required this.onCheck});
  final VoidCallback onCheck;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(Icons.system_update_alt, color: Theme.of(context).colorScheme.onSurfaceVariant),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Text(
            'Check for new firmware',
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ),
        TextButton(
          onPressed: onCheck,
          child: const Text('Check'),
        ),
      ],
    );
  }
}

class _BusyView extends StatelessWidget {
  const _BusyView({required this.label});
  final String label;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        const SizedBox(
          width: 18, height: 18, // deliberate dimension, not spacing
          child: CircularProgressIndicator(strokeWidth: 2),
        ),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Text(
            label,
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ),
      ],
    );
  }
}

class _ReadyView extends StatelessWidget {
  const _ReadyView({
    required this.versionString,
    required this.channel,
    required this.imageBytes,
    required this.onInstall,
    required this.onCancel,
  });
  final String versionString;
  final String channel;
  final int imageBytes;
  final VoidCallback onInstall;
  final VoidCallback onCancel;

  @override
  Widget build(BuildContext context) {
    final mb = (imageBytes / 1024 / 1024).toStringAsFixed(2);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(
          'Update available: v$versionString ($channel)',
          style: Theme.of(context).textTheme.titleMedium,
        ),
        const SizedBox(height: AppSpace.xs),
        Text(
          'Signed image $mb MB. Push will take ~30 seconds.',
          style: Theme.of(context).textTheme.bodySmall,
        ),
        const SizedBox(height: AppSpace.md),
        Row(
          mainAxisAlignment: MainAxisAlignment.end,
          children: [
            TextButton(onPressed: onCancel, child: const Text('Not now')),
            const SizedBox(width: AppSpace.sm),
            FilledButton(onPressed: onInstall, child: const Text('Install')),
          ],
        ),
      ],
    );
  }
}

class _StreamingView extends StatelessWidget {
  const _StreamingView({
    required this.chunksSent,
    required this.totalChunks,
    required this.progress,
    required this.onCancel,
  });
  final int chunksSent;
  final int totalChunks;
  final double progress;
  final VoidCallback onCancel;

  @override
  Widget build(BuildContext context) {
    final pct = (progress * 100).clamp(0, 100).toStringAsFixed(0);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                'Installing… $pct%',
                style: Theme.of(context).textTheme.titleMedium,
              ),
            ),
            TextButton(onPressed: onCancel, child: const Text('Cancel')),
          ],
        ),
        const SizedBox(height: AppSpace.sm),
        LinearProgressIndicator(value: progress),
        const SizedBox(height: AppSpace.xs),
        Text(
          '$chunksSent / $totalChunks chunks',
          style: Theme.of(context).textTheme.bodySmall,
        ),
      ],
    );
  }
}

class _SucceededView extends StatelessWidget {
  const _SucceededView({required this.versionString, required this.onDismiss});
  final String versionString;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(Icons.check_circle, color: context.brandExtras.success),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Text(
            'Installed v$versionString. The lamp is rebooting.',
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ),
        TextButton(onPressed: onDismiss, child: const Text('OK')),
      ],
    );
  }
}

class _FailedView extends StatelessWidget {
  const _FailedView({required this.reason, required this.onRetry});
  final String reason;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(Icons.error_outline, color: Theme.of(context).colorScheme.secondary),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Text(
            reason,
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ),
        TextButton(onPressed: onRetry, child: const Text('Dismiss')),
      ],
    );
  }
}
