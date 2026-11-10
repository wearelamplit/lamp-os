import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/theme/brand_extras.dart';
import '../../../core/widgets/info_panel.dart';
import '../../control/application/control_notifier.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../application/cached_firmware_notifier.dart';
import '../application/firmware_gate.dart';
import '../application/firmware_notifier.dart';
import '../data/cached_firmware.dart';
import '../domain/firmware_state.dart';
import 'firmware_copy.dart';

class FirmwareUpdatePanel extends ConsumerWidget {
  const FirmwareUpdatePanel({
    super.key,
    required this.deviceId,
    required this.lampType,
    required this.lampChannel,
    required this.lampFwVersion,
  });

  final String deviceId;
  final String? lampType;
  final String? lampChannel;
  final int? lampFwVersion;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(firmwareNotifierProvider(deviceId));
    final notifier = ref.read(firmwareNotifierProvider(deviceId).notifier);
    final name = ref.watch(controlNotifierProvider(deviceId)
        .select((s) => FirmwareCopy.nameOf(s.value)));

    // Fall back to inventory's persisted values when the live LampSection
    // hasn't reported lampType / fwChannel / fwVersion yet (pre-section-read
    // window, or a lamp whose firmware is too old to emit the field). Without
    // this, the persisted values are write-only and gating degrades.
    String? resolvedLampType = lampType;
    String? resolvedChannel = lampChannel;
    int? resolvedFwVersion = lampFwVersion;
    if (resolvedLampType == null ||
        resolvedChannel == null ||
        resolvedFwVersion == null) {
      final inv = ref.watch(inventoryNotifierProvider).value;
      if (inv != null) {
        for (final entry in inv) {
          if (entry.id == deviceId) {
            resolvedLampType ??= entry.lampType;
            resolvedChannel ??= entry.fwChannel;
            resolvedFwVersion ??= entry.fwVersion;
            break;
          }
        }
      }
    }

    return InfoPanel(
      child: switch (state) {
        FirmwareIdle() => _CacheListView(
            deviceId: deviceId,
            lampType: resolvedLampType,
            lampChannel: resolvedChannel,
            lampFwVersion: resolvedFwVersion,
          ),
        FirmwareVerifying() => const _BusyView(label: 'Verifying signature…'),
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
        FirmwareSucceeded(:final footer, :final alreadyCurrent) =>
          _SucceededView(
            message: alreadyCurrent
                ? FirmwareCopy.alreadyKnows(name)
                : 'Installed v${footer.versionString}. The lamp is rebooting.',
            onDismiss: notifier.reset,
          ),
        FirmwareFailed(:final failure, :final reason) => _FailedView(
            friendly: FirmwareCopy.failure(failure, name),
            reason: reason,
            onRetry: notifier.reset,
          ),
      },
    );
  }
}

class _CacheListView extends ConsumerWidget {
  const _CacheListView({
    required this.deviceId,
    required this.lampType,
    required this.lampChannel,
    required this.lampFwVersion,
  });
  final String deviceId;
  final String? lampType;
  final String? lampChannel;
  final int? lampFwVersion;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final entries = ref.watch(cachedFirmwareNotifierProvider).value ??
        const <String, CachedFirmware>{};
    final theme = Theme.of(context);

    if (entries.isEmpty) {
      return Row(
        children: [
          Icon(Icons.cloud_off,
              color: theme.colorScheme.onSurfaceVariant),
          const SizedBox(width: AppSpace.md),
          Expanded(
            child: Text(
              'No firmware downloaded yet. It downloads when the app starts online.',
              style: theme.textTheme.bodyLarge,
            ),
          ),
        ],
      );
    }

    final sorted = entries.values.toList()
      ..sort((a, b) => a.key.compareTo(b.key));

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (final entry in sorted)
          _CacheRow(
            entry: entry,
            action: firmwareRowActionFor(
              entry: entry,
              lampType: lampType,
              lampChannel: lampChannel,
              lampFwVersion: lampFwVersion,
            ),
            onInstall: () => ref
                .read(firmwareNotifierProvider(deviceId).notifier)
                .installFromCache(
                  lampType: entry.lampType,
                  channel: entry.channel,
                ),
            onDelete: () => ref
                .read(cachedFirmwareNotifierProvider.notifier)
                .delete(lampType: entry.lampType, channel: entry.channel),
          ),
      ],
    );
  }
}

class _CacheRow extends StatelessWidget {
  const _CacheRow({
    required this.entry,
    required this.action,
    required this.onInstall,
    required this.onDelete,
  });
  final CachedFirmware entry;
  final FirmwareRowAction action;
  final VoidCallback onInstall;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final label =
        '${entry.lampType} · v${entry.versionString} (${entry.channel.name})';

    final Widget trailing = switch (action) {
      FirmwareRowAction.install =>
        TextButton(onPressed: onInstall, child: const Text('Install')),
      FirmwareRowAction.upToDate => Text(
          'up to date',
          style: theme.textTheme.bodySmall
              ?.copyWith(color: theme.colorScheme.onSurfaceVariant),
        ),
      FirmwareRowAction.notThisLamp => Text(
          'not this lamp',
          style: theme.textTheme.bodySmall
              ?.copyWith(color: theme.colorScheme.onSurfaceVariant),
        ),
      FirmwareRowAction.versionUnknown => Text(
          'version unknown',
          style: theme.textTheme.bodySmall
              ?.copyWith(color: theme.colorScheme.onSurfaceVariant),
        ),
    };

    return Row(
      children: [
        Expanded(child: Text(label, style: theme.textTheme.bodyLarge)),
        const SizedBox(width: AppSpace.sm),
        trailing,
        IconButton(
          onPressed: onDelete,
          icon: const Icon(Icons.delete_outline),
          tooltip: 'Remove download',
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
  const _SucceededView({required this.message, required this.onDismiss});
  final String message;
  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(Icons.check_circle, color: context.brandExtras.success),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Text(
            message,
            style: Theme.of(context).textTheme.bodyLarge,
          ),
        ),
        TextButton(onPressed: onDismiss, child: const Text('OK')),
      ],
    );
  }
}

class _FailedView extends StatelessWidget {
  const _FailedView({
    required this.friendly,
    required this.reason,
    required this.onRetry,
  });
  final String friendly;
  final String reason;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      children: [
        Icon(Icons.error_outline, color: theme.colorScheme.secondary),
        const SizedBox(width: AppSpace.md),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(friendly, style: theme.textTheme.bodyLarge),
              const SizedBox(height: AppSpace.xs),
              SelectableText(
                reason,
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.onSurfaceVariant,
                  fontFamily: 'monospace',
                ),
              ),
            ],
          ),
        ),
        TextButton(onPressed: onRetry, child: const Text('Dismiss')),
      ],
    );
  }
}
