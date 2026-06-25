// Owns the per-lamp firmware OTA lifecycle: download → verify → push.
//
// One notifier per lamp (keyed by deviceId). Sequence:
//   1. State starts at FirmwareIdle.
//   2. checkForUpdate() → FirmwareDownloading → FirmwareVerifying →
//      FirmwareReadyToInstall (with parsed footer + image kept in
//      memory for the install step).
//   3. install() consumes the kept image, transitions through
//      FirmwareOfferSent / FirmwareStreaming / FirmwareFinalizing,
//      then FirmwareSucceeded or FirmwareFailed.
//   4. reset() returns to FirmwareIdle (called after user dismisses
//      a success/fail card).
//
// The notifier holds the downloaded image in memory across the verify
// → ready → install sequence. ~1.5 MB sits in a single Uint8List —
// fine for a phone. We don't cache to disk for v1; the user can
// re-tap "Check for updates" if they back out and come back.

import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client_provider.dart';
import '../data/firmware_ota_pusher.dart';
import '../data/firmware_release_client.dart';
import '../data/firmware_release_client_provider.dart';
import '../data/firmware_signature_verifier.dart';
import '../domain/firmware_state.dart';
import '../domain/lsig_footer.dart';
import 'cached_firmware_notifier.dart';

part 'firmware_notifier.g.dart';

@Riverpod(name: 'firmwareNotifierProvider')
class FirmwareNotifier extends _$FirmwareNotifier {
  Uint8List? _pendingImage;
  VerifiedFirmware? _pendingVerified;
  FirmwareOtaPusher? _activePusher;

  @override
  FirmwareState build(String deviceId) {
    ref.onDispose(() {
      _activePusher?.cancel();
      _activePusher = null;
      _pendingImage = null;
      _pendingVerified = null;
    });
    return const FirmwareIdle();
  }

  /// User-facing: check for an update on the configured channel.
  /// Downloads the per-variant binary for [lampType], verifies it, and
  /// cross-checks that the verified LSIG footer's channel string equals
  /// `"$lampType-${channel.name}"` (defense-in-depth against a CI mistake
  /// that uploaded the wrong asset under the right name). On success
  /// leaves state at [FirmwareReadyToInstall].
  Future<void> checkForUpdate({
    FirmwareChannel channel = FirmwareChannel.stable,
    required String? lampType,
  }) async {
    if (lampType == null || lampType.isEmpty) {
      state = const FirmwareFailed(
        reason:
            'This lamp has not reported its variant identity yet. Reconnect '
            'once and try again.',
      );
      return;
    }
    state = const FirmwareDownloading();
    Uint8List image;
    // Try the disk cache first — CachedFirmwareNotifier proactively pulls
    // the per-variant binary on inventory change, so the common case is
    // a hit and we skip the network entirely (works offline).
    final cache = ref.read(cachedFirmwareNotifierProvider.notifier);
    final cached =
        await cache.bytesFor(lampType: lampType, channel: channel);
    if (cached != null) {
      image = cached.bytes;
    } else {
      final client = ref.read(firmwareReleaseClientProvider);
      try {
        image = await client.fetchLatest(channel, lampType: lampType);
      } catch (e) {
        state = FirmwareFailed(
          reason: 'Could not reach the update server and no cached '
              'firmware is available for this lamp.',
          cause: e,
        );
        return;
      }
    }

    state = const FirmwareVerifying();
    VerifiedFirmware verified;
    try {
      verified = await verifyFirmwareImage(image);
    } catch (e) {
      state = FirmwareFailed(
        reason: 'The downloaded firmware failed signature verification.',
        cause: e,
      );
      return;
    }

    final expectedChannel = '$lampType-${channel.name}';
    if (verified.footer.channel != expectedChannel) {
      state = FirmwareFailed(
        reason:
            'Downloaded firmware is for the wrong lamp type or channel '
            '(expected "$expectedChannel", got "${verified.footer.channel}").',
      );
      return;
    }

    _pendingImage    = image;
    _pendingVerified = verified;
    state = FirmwareReadyToInstall(
      footer: verified.footer,
      imageBytes: image.length,
    );
  }

  /// User-facing: push the previously-downloaded + verified image to the
  /// lamp. Requires a prior [checkForUpdate] that left state at
  /// [FirmwareReadyToInstall]. The Riverpod connection from the BLE
  /// client provider drives the actual writes.
  Future<void> install() async {
    final image    = _pendingImage;
    final verified = _pendingVerified;
    if (image == null || verified == null) {
      state = const FirmwareFailed(
        reason: 'No verified update is queued. Tap "Check for updates" first.',
      );
      return;
    }

    final ble = ref.read(bleClientProvider);

    final pusher = FirmwareOtaPusher(
      ble: ble,
      deviceId: deviceId,
      signedImage: image,
      version: verified.footer.version,
      channel: verified.footer.channel,
      sha256Prefix: verified.sha256Prefix,
      totalLen: image.length,
      footerLen: lsigFooterLen,
      onProgress: (sent, total) {
        if (state is FirmwareStreaming || state is FirmwareOfferSent) {
          state = FirmwareStreaming(
            footer: verified.footer,
            chunksSent: sent,
            totalChunks: total,
          );
        }
      },
    );
    _activePusher = pusher;

    state = FirmwareOfferSent(footer: verified.footer);
    final result = await pusher.start();
    _activePusher = null;

    if (result.success) {
      state = FirmwareSucceeded(footer: verified.footer);
      // Drop the in-memory bytes — the lamp is rebooting.
      _pendingImage    = null;
      _pendingVerified = null;
    } else {
      state = FirmwareFailed(reason: result.message);
    }
    debugPrint('[firmware] OTA result: ${result.message}');
  }

  /// Cancel an in-flight install and return to idle.
  void cancel() {
    _activePusher?.cancel();
    _activePusher = null;
    reset();
  }

  /// Return to idle. Called from the UI's "dismiss" action on a
  /// success or failure card.
  void reset() {
    _pendingImage    = null;
    _pendingVerified = null;
    state = const FirmwareIdle();
  }
}
