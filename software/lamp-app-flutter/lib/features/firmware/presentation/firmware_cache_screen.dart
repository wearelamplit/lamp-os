// Advanced-mode list of firmware binaries the app has cached locally.
// Sourced from CachedFirmwareNotifier — one row per {lampType, channel}
// pair the user currently owns. Each row shows the version, file size,
// fetched-at age, and a kebab menu for delete.

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/brand_colors.dart';
import '../application/cached_firmware_notifier.dart';
import '../data/cached_firmware.dart';

class FirmwareCacheScreen extends ConsumerWidget {
  const FirmwareCacheScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final asyncCache = ref.watch(cachedFirmwareNotifierProvider);
    return Scaffold(
      backgroundColor: BrandColors.midnightBlack,
      appBar: AppBar(
        title: const Text('Cached firmwares'),
        backgroundColor: BrandColors.midnightBlack,
        foregroundColor: BrandColors.lampWhite,
      ),
      body: asyncCache.when(
        loading: () =>
            const Center(child: CircularProgressIndicator(strokeWidth: 2)),
        error: (e, _) => Center(
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Text(
              'Cache index could not be loaded: $e',
              style: const TextStyle(color: BrandColors.fogGrey),
            ),
          ),
        ),
        data: (cache) {
          final entries = cache.values.toList()
            ..sort((a, b) => a.key.compareTo(b.key));
          if (entries.isEmpty) {
            return const Padding(
              padding: EdgeInsets.all(24),
              child: Text(
                'No firmware binaries cached yet. The app fetches one per '
                'lamp variant + channel you own; sync runs in the '
                'background when you adopt a lamp or open the app online.',
                style: TextStyle(color: BrandColors.fogGrey, fontSize: 13),
              ),
            );
          }
          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: 8),
            itemCount: entries.length,
            separatorBuilder: (_, _) => const Divider(
              color: BrandColors.midnightBlack,
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
    return ListTile(
      title: Text(
        '${entry.lampType} · ${entry.channel.name}',
        style: const TextStyle(
          color: BrandColors.lampWhite,
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
      ),
      subtitle: Text(
        'v${entry.versionString} · ${_formatSize(entry.byteLength)} · '
        'fetched ${_formatAge(entry.fetchedAtMs)}',
        style: const TextStyle(color: BrandColors.slateGrey, fontSize: 12),
      ),
      trailing: IconButton(
        icon: const Icon(Icons.delete_outline, color: BrandColors.slateGrey),
        tooltip: 'Remove from cache',
        onPressed: () => ref
            .read(cachedFirmwareNotifierProvider.notifier)
            .delete(lampType: entry.lampType, channel: entry.channel),
      ),
    );
  }
}
