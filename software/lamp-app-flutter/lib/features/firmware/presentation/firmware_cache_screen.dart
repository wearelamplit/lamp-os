// Advanced-mode list of firmware binaries the app has cached locally.
// Sourced from CachedFirmwareNotifier — one row per {lampType, channel}
// pair the user currently owns. Each row shows the version, file size,
// fetched-at age, and a kebab menu for delete.

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../application/cached_firmware_notifier.dart';
import '../data/cached_firmware.dart';

class FirmwareCacheScreen extends ConsumerWidget {
  const FirmwareCacheScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final asyncCache = ref.watch(cachedFirmwareNotifierProvider);
    final colorScheme = Theme.of(context).colorScheme;
    return Scaffold(
      appBar: AppBar(
        title: const Text('Cached firmwares'),
      ),
      body: asyncCache.when(
        loading: () =>
            const Center(child: CircularProgressIndicator(strokeWidth: 2)),
        error: (e, _) => Center(
          child: Padding(
            padding: const EdgeInsets.all(AppSpace.xl),
            child: Text(
              'Cache index could not be loaded: $e',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
        ),
        data: (cache) {
          final entries = cache.values.toList()
            ..sort((a, b) => a.key.compareTo(b.key));
          if (entries.isEmpty) {
            return Padding(
              padding: const EdgeInsets.all(AppSpace.xl),
              child: Text(
                'No firmware binaries cached yet. The app fetches one per '
                'lamp variant + channel you own; sync runs in the '
                'background when you adopt a lamp or open the app online.',
                style: Theme.of(context).textTheme.bodyMedium,
              ),
            );
          }
          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
            itemCount: entries.length,
            separatorBuilder: (_, _) => Divider(
              color: colorScheme.outline,
              height: 1,
            ),
            itemBuilder: (context, i) => _CacheRow(entry: entries[i]),
          );
        },
      ),
    );
  }
}

class _CacheRow extends ConsumerWidget {
  const _CacheRow({required this.entry});
  final CachedFirmware entry;

  String _formatAge(int fetchedAtMs) {
    final age = DateTime.now().millisecondsSinceEpoch - fetchedAtMs;
    if (age < 60 * 1000) return 'just now';
    final mins = age ~/ (60 * 1000);
    if (mins < 60) return '${mins}m ago';
    final hours = mins ~/ 60;
    if (hours < 24) return '${hours}h ago';
    return '${hours ~/ 24}d ago';
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(0)} KB';
    return '${(bytes / 1024 / 1024).toStringAsFixed(1)} MB';
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final textTheme = Theme.of(context).textTheme;
    final colorScheme = Theme.of(context).colorScheme;
    return ListTile(
      title: Text(
        '${entry.lampType} · ${entry.channel.name}',
        style: textTheme.titleMedium,
      ),
      subtitle: Text(
        'v${entry.versionString} · ${_formatSize(entry.byteLength)} · '
        'fetched ${_formatAge(entry.fetchedAtMs)}',
        style: textTheme.bodySmall,
      ),
      trailing: IconButton(
        icon: Icon(Icons.delete_outline, color: colorScheme.onSurfaceVariant),
        tooltip: 'Remove from cache',
        onPressed: () => ref
            .read(cachedFirmwareNotifierProvider.notifier)
            .delete(lampType: entry.lampType, channel: entry.channel),
      ),
    );
  }
}
