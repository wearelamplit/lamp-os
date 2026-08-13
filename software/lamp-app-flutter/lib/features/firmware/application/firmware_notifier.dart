import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client_provider.dart';
import '../data/firmware_ota_pusher.dart';
import '../data/firmware_release_client.dart';
import '../data/firmware_signature_verifier.dart';
import '../domain/firmware_state.dart';
import '../domain/lsig_footer.dart';
import 'cached_firmware_notifier.dart';

part 'firmware_notifier.g.dart';

@Riverpod(name: 'firmwareNotifierProvider')
class FirmwareNotifier extends _$FirmwareNotifier {
  FirmwareOtaPusher? _activePusher;

  @override
  FirmwareState build(String deviceId) {
    ref.onDispose(() {
      _activePusher?.cancel();
      _activePusher = null;
    });
    return const FirmwareIdle();
  }

  /// Push a firmware image already downloaded to the on-disk cache. Loads
  /// the bytes for [lampType] + [channel], verifies the LSIG signature, and
  /// cross-checks that the footer channel equals `"$lampType-${channel.name}"`
  /// (defense-in-depth against a CI mistake that uploaded the wrong asset
  /// under the right name) before streaming it to the lamp.
  Future<void> installFromCache({
    required String lampType,
    required FirmwareChannel channel,
  }) async {
    if (state is FirmwareVerifying || _activePusher != null) return;
    // Close the double-tap window synchronously, before the first await.
    state = const FirmwareVerifying();

    final cache = ref.read(cachedFirmwareNotifierProvider.notifier);
    final blob = await cache.bytesFor(lampType: lampType, channel: channel);
    if (blob == null) {
      state = const FirmwareFailed(
        reason: 'That firmware is no longer in the download cache.',
      );
      return;
    }

    VerifiedFirmware verified;
    try {
      verified = await verifyFirmwareImage(blob.bytes);
    } catch (e) {
      state = FirmwareFailed(
        reason: 'The cached firmware failed signature verification.',
        cause: e,
      );
      return;
    }

    final expectedChannel = '$lampType-${channel.name}';
    if (verified.footer.channel != expectedChannel) {
      state = FirmwareFailed(
        reason: 'Cached firmware is for the wrong lamp type or channel '
            '(expected "$expectedChannel", got "${verified.footer.channel}").',
      );
      return;
    }

    await _push(blob.bytes, verified);
  }

  Future<void> _push(Uint8List image, VerifiedFirmware verified) async {
    final ble = ref.read(bleClientProvider);

    final pusher = FirmwareOtaPusher(
      ble: ble,
      deviceId: deviceId,
      signedImage: image,
      version: verified.footer.version,
      channel: verified.footer.channel,
      digest: verified.fullSha256,
      signature: verified.footer.signature,
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

    // Pin this notifier alive for the duration of the stream. Without it,
    // tabbing away from the Info screen unmounts FirmwareUpdatePanel, the
    // provider autoDisposes, onDispose fires `_activePusher.cancel()`, and
    // the OTA silently aborts mid-push. Released in `finally` so the notifier
    // can dispose normally once the stream is done.
    final keepAlive = ref.keepAlive();
    try {
      state = FirmwareOfferSent(footer: verified.footer);
      final result = await pusher.start();
      _activePusher = null;

      if (result.success) {
        state = FirmwareSucceeded(footer: verified.footer);
      } else {
        state = FirmwareFailed(reason: result.message);
      }
      debugPrint('[firmware] OTA result: ${result.message}');
    } finally {
      keepAlive.close();
    }
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
    state = const FirmwareIdle();
  }
}
