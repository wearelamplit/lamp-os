import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/theme/brand_extras.dart';
import '../../../core/widgets/info_panel.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../application/cached_firmware_notifier.dart';
import '../application/firmware_notifier.dart';
import '../data/cached_firmware.dart';
import '../domain/firmware_state.dart';

enum FirmwareRowAction { install, upToDate, notThisLamp, versionUnknown }

/// Gate a cached entry against the lamp: [install] only when the variant
/// matches, the lamp reported its running version, and the cached build is
/// strictly newer. Same packed-semver encoding on both sides, so `>` is a
/// correct newer-than test.
FirmwareRowAction firmwareRowActionFor({
  required CachedFirmware entry,
  required String? lampType,
  required int? lampFwVersion,
}) {
  if (entry.lampType != lampType) return FirmwareRowAction.notThisLamp;
  if (lampFwVersion == null) return FirmwareRowAction.versionUnknown;
  if (entry.version <= lampFwVersion) return FirmwareRowAction.upToDate;
  return FirmwareRowAction.install;
}

class FirmwareUpdatePanel extends ConsumerWidget {
  const FirmwareUpdatePanel({
    super.key,
    required this.deviceId,
    required this.lampType,
    required this.lampFwVersion,
  });

  final String deviceId;
  final String? lampType;
  final int? lampFwVersion;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(firmwareNotifierProvider(deviceId));
    final notifier = ref.read(firmwareNotifierProvider(deviceId).notifier);

    // Fall back to inventory's persisted values when the live LampSection
    // hasn't reported lampType / fwVersion yet (pre-section-read window, or
    // a lamp whose firmware is too old to emit the field). Without this, the
    // persisted values are write-only and gating degrades unnecessarily.
    String? resolvedLampType = lampType;
    int? resolvedFwVersion = lampFwVersion;
    if (resolvedLampType == null || resolvedFwVersion == null) {
      final inv = ref.watch(inventoryNotifierProvider).value;
      if (inv != null) {
        for (final entry in inv) {
          if (entry.id == deviceId) {
            resolvedLampType ??= entry.lampType;
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

class _CacheListView extends ConsumerWidget {
  const _CacheListView({
    required this.deviceId,
    required this.lampType,
    required this.lampFwVersion,
  });
  final String deviceId;
  final String? lampType;
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
              'No firmware downloaded yet — connect while online to sync.',
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
        Text('Downloaded firmware', style: theme.textTheme.titleMedium),
        const SizedBox(height: AppSpace.sm),
        for (final entry in sorted)
          _CacheRow(
            entry: entry,
            action: firmwareRowActionFor(
              entry: entry,
              lampType: lampType,
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
