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

/// All chunks acked; DONE on the wire; waiting for the lamp's RESULT.
class FirmwareFinalizing extends FirmwareState {
  const FirmwareFinalizing({required this.footer});
  final LsigFooter footer;
}

/// Lamp reported success; will reboot momentarily.
class FirmwareSucceeded extends FirmwareState {
  const FirmwareSucceeded({required this.footer});
  final LsigFooter footer;
}

/// Terminal failure surface. Wraps a user-presentable reason. The
/// `cause` is preserved for logging but not shown to the user.
class FirmwareFailed extends FirmwareState {
  const FirmwareFailed({required this.reason, this.cause});
  final String reason;
  final Object? cause;
}
