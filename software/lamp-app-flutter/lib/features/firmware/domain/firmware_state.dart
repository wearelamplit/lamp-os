// OTA state types for the firmware notifier. Sealed class so the UI
// can exhaustively switch on it.

import 'lsig_footer.dart';

sealed class FirmwareState {
  const FirmwareState();
}

/// No OTA in progress. Shows the downloaded-firmware list.
class FirmwareIdle extends FirmwareState {
  const FirmwareIdle();
}

/// Verifying the LSIG footer + Ed25519 signature locally.
class FirmwareVerifying extends FirmwareState {
  const FirmwareVerifying();
}

/// OFFER is on the wire; waiting for ACCEPT.
class FirmwareOfferSent extends FirmwareState {
  const FirmwareOfferSent({required this.footer});
  final LsigFooter footer;
}

/// Streaming chunks. `chunksSent` / `totalChunks` drive the progress
/// bar; the UI also surfaces the chunk-rate when nonzero.
class FirmwareStreaming extends FirmwareState {
  const FirmwareStreaming({
    required this.footer,
    required this.chunksSent,
    required this.totalChunks,
  });
  final LsigFooter footer;
  final int chunksSent;
  final int totalChunks;

  /// 0..1 progress fraction; safe against totalChunks == 0.
  double get progress {
    if (totalChunks == 0) return 0.0;
    return chunksSent / totalChunks;
  }
}

/// Every chunk is sent; DONE follows and the app waits for the lamp's RESULT.
class FirmwareFinalizing extends FirmwareState {
  const FirmwareFinalizing({required this.footer});
  final LsigFooter footer;
}

/// Lamp reported success; will reboot momentarily.
class FirmwareSucceeded extends FirmwareState {
  const FirmwareSucceeded({required this.footer, this.alreadyCurrent = false});
  final LsigFooter footer;

  /// The lamp already runs this version or newer and turned the offer down.
  /// It installs nothing and does not reboot.
  final bool alreadyCurrent;
}

/// Why a push failed, as the app explains it to the lamp's owner.
enum FirmwareFailure {
  noAnswer,
  setupFailed,
  busy,
  untrustedAtOffer,
  untrusted,
  linkLost,
  missingPieces,
  noResult,
  scrambled,
  storeFailed,
  unknown,
  cacheMissing,
  cacheUnverified,
  wrongLamp,
  cancelled;

  /// The lamp turned the offer down, so no lesson ran.
  bool get declined => this == busy || this == untrustedAtOffer;
}

/// Terminal failure surface. [reason] is the technical account, shown only
/// in dev mode beneath the friendly line for [failure]. The `cause` is
/// preserved for logging but not shown.
class FirmwareFailed extends FirmwareState {
  const FirmwareFailed({
    required this.reason,
    required this.failure,
    this.cause,
  });
  final String reason;
  final FirmwareFailure failure;
  final Object? cause;
}

extension FirmwarePushing on FirmwareState {
  /// The phone is actively pushing an image to this lamp (offer through
  /// finalize). Excludes Succeeded so the post-reboot presence takes over.
  bool get isPushing => pushedVersion != null;

  /// Pushing plus the local verify step that precedes OfferSent. Drives the
  /// whimsical badge + card so the verify window isn't a blank gap.
  bool get isBusy => isPushing || this is FirmwareVerifying;

  /// The version being pushed, from the offer until the lamp's result; null
  /// in every other state.
  int? get pushedVersion => switch (this) {
    FirmwareOfferSent(:final footer) ||
    FirmwareStreaming(:final footer) ||
    FirmwareFinalizing(:final footer) => footer.version,
    _ => null,
  };
}

/// The push state once [chunksSent] of [totalChunks] are sent: streaming
/// until the last chunk is out, then finalizing while the lamp verifies and
/// reports.
FirmwareState pushProgress(
  LsigFooter footer, {
  required int chunksSent,
  required int totalChunks,
}) => chunksSent >= totalChunks
    ? FirmwareFinalizing(footer: footer)
    : FirmwareStreaming(
        footer: footer,
        chunksSent: chunksSent,
        totalChunks: totalChunks,
      );
