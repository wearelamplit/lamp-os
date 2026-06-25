// BLE OTA pusher — drives one signed firmware image into one connected
// lamp using the Phase 5a CHAR_FW_CONTROL + CHAR_FW_CHUNK characteristics.
//
// State machine (mirrors the lamp-side distributor enum but lives on the
// SENDER side):
//
//   .start() called
//     → write OFFER to CHAR_FW_CONTROL (with-response)
//     → wait for ACCEPT notify on CHAR_FW_CONTROL
//   onAccept(Accept):
//     → stream CHUNKs to CHAR_FW_CHUNK (write-no-response)
//     → after last chunk: write DONE to CHAR_FW_CONTROL
//     → wait for RESULT notify
//   onAccept(DeclineBusy / DeclineAlreadyCurrent):
//     → completer.complete with a categorized error
//   onReq(firstIdx, count):
//     → rewind chunk cursor to firstIdx; the stream loop picks up
//   onResult(Success):
//     → completer.complete success (the lamp reboots immediately)
//   onResult(<error>):
//     → completer.complete with the FwResultStatus mapped to a user-
//       facing message
//
// Streaming pacing:
//   - 10 ms gap between chunks (kStreamingChunkSpacingMs on the lamp).
//     Goes hand-in-hand with the receiver's loop-task starvation
//     concern (esp_ota_write_with_offset flash-bus contention) — same
//     as the gossip-OTA mesh streaming cadence. Tested on hardware via
//     Phase 5a hand-flash; ~80 chunks/sec sustained for the full image.
//   - chunkSize = 200 bytes (FW_CHUNK_SIZE locked in v1).

import 'dart:async';
import 'dart:typed_data';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/uuids.dart';
import '../domain/firmware_protocol.dart';

/// Result of a single OTA push attempt. Returned from
/// [FirmwareOtaPusher.start].
class FirmwareOtaResult {
  const FirmwareOtaResult({
    required this.success,
    required this.message,
    this.lampReportedVersion,
  });

  /// True iff the lamp reported [FwResultStatus.success] and is about
  /// to reboot. Caller should NOT attempt to keep the BLE connection
  /// — the lamp drops it within ~100 ms.
  final bool success;

  /// Short user-facing string. On success: a "shipped" confirmation.
  /// On failure: a categorized message (e.g. "already up to date",
  /// "another update in progress", "signature mismatch").
  final String message;

  /// Lamp's report of the version it ended up on. Populated on success;
  /// null on failure.
  final int? lampReportedVersion;
}

typedef ChunkProgressCallback = void Function(int chunksSent, int totalChunks);

class FirmwareOtaPusher {
  FirmwareOtaPusher({
    required this.ble,
    required this.deviceId,
    required this.signedImage,
    required this.version,
    required this.channel,
    required this.sha256Prefix,
    required this.totalLen,
    required this.footerLen,
    this.appMac = const AppSourceMac(),
    this.onProgress,
    this.chunkSpacingMs = 10,
    this.acceptTimeout = const Duration(seconds: 10),
    this.resultTimeout = const Duration(seconds: 40),
  });

  final BleClient ble;
  final String deviceId;
  final Uint8List signedImage;
  final int version;
  final String channel;
  final Uint8List sha256Prefix;
  final int totalLen;
  final int footerLen;
  final AppSourceMac appMac;
  final ChunkProgressCallback? onProgress;
  final int chunkSpacingMs;
  final Duration acceptTimeout;
  final Duration resultTimeout;

  // ── Session state ──────────────────────────────────────────────────
  Completer<FirmwareOtaResult>? _completer;
  StreamSubscription<Uint8List>? _notifySub;
  bool _streamingActive = false;
  int _offerSeq = 0;
  int _chunkSeq = 0;
  int _doneSeq  = 0;
  int _nextChunkIdx = 0;
  int _chunksSent   = 0;
  late final int _totalChunks;
  // Target MAC. The lamp dedups frames by (sourceMac, targetMac) — the
  // sender (us, the app) doesn't know the lamp's MAC up front. The BLE
  // path on the lamp uses the BLE connection handle for routing rather
  // than the targetMac field, so we can ship zeros here without
  // affecting correctness (mirror of how the lamp-side BLE OTA pusher
  // populates targetMac in Phase 5a).
  static final Uint8List _zeroMac = Uint8List(6);

  /// Start the OTA push. Returns a future that completes when the lamp
  /// reports a terminal status. Throws if a session is already running.
  Future<FirmwareOtaResult> start() async {
    if (_completer != null) {
      throw StateError('FirmwareOtaPusher already running');
    }
    _completer = Completer<FirmwareOtaResult>();
    _totalChunks = (signedImage.length + fwChunkSize - 1) ~/ fwChunkSize;

    // Subscribe BEFORE the OFFER write so a near-instant ACCEPT can't
    // race us.
    try {
      _notifySub = ble
          .subscribe(deviceId, BleUuids.controlService, BleUuids.fwControl)
          .listen(_onNotify, onError: _onNotifyError);

      _offerSeq = _nextSeq();
      final offer = buildFwOffer(
        seq: _offerSeq,
        sourceMac: appMac.bytes,
        targetMac: _zeroMac,
        version: version,
        totalLen: totalLen,
        chunkSize: fwChunkSize,
        channel: channel,
        sha256Prefix: sha256Prefix,
        footerLen: footerLen,
        totalChunks: _totalChunks,
      );
      await ble.write(
        deviceId,
        BleUuids.controlService,
        BleUuids.fwControl,
        offer,
      );

      // ACCEPT timeout watchdog — we hold the completer and a stuck
      // peer would otherwise wedge the future forever.
      Future.delayed(acceptTimeout, () {
        if (_completer != null && !_completer!.isCompleted && !_streamingActive) {
          _finish(const FirmwareOtaResult(
            success: false,
            message: 'Lamp did not respond to the offer.',
          ));
        }
      });
    } catch (e) {
      _finish(FirmwareOtaResult(success: false, message: 'OTA setup failed: $e'));
    }
    return _completer!.future;
  }

  void _onNotify(Uint8List data) {
    if (_completer == null || _completer!.isCompleted) return;
    final t = inspectHeader(data);
    if (t == msgFwAccept) {
      final a = parseFwAccept(data);
      if (a == null || a.offerSeq != _offerSeq) return;
      switch (a.status) {
        case FwAcceptStatus.accept:
          _streamingActive = true;
          unawaited(_runStreamingLoop());
          break;
        case FwAcceptStatus.declineBusy:
          _finish(const FirmwareOtaResult(
            success: false,
            message: 'Another update is already in progress on this lamp.',
          ));
          break;
        case FwAcceptStatus.declineAlreadyCurrent:
          _finish(const FirmwareOtaResult(
            success: true,  // Not technically success, but no work to do.
            message: 'Lamp is already running this version or newer.',
          ));
          break;
      }
    } else if (t == msgFwReq) {
      final r = parseFwReq(data);
      if (r == null) return;
      // Rewind cursor. The streaming loop respects _nextChunkIdx on
      // every iteration, so this is enough to redirect it.
      if (r.firstChunkIdx < _totalChunks) {
        _nextChunkIdx = r.firstChunkIdx;
      }
    } else if (t == msgFwResult) {
      final res = parseFwResult(data);
      if (res == null) return;
      _streamingActive = false;
      _finish(_mapResultToOtaResult(res));
    }
  }

  void _onNotifyError(Object err) {
    if (_completer == null || _completer!.isCompleted) return;
    _finish(FirmwareOtaResult(
      success: false,
      message: 'BLE notification stream error: $err',
    ));
  }

  Future<void> _runStreamingLoop() async {
    while (_streamingActive &&
           _nextChunkIdx < _totalChunks &&
           _completer != null && !_completer!.isCompleted) {
      final idx = _nextChunkIdx;
      final start = idx * fwChunkSize;
      final end = (start + fwChunkSize) < signedImage.length
          ? (start + fwChunkSize)
          : signedImage.length;
      final payload = Uint8List.sublistView(signedImage, start, end);
      final frame = buildFwChunk(
        seq: _nextSeq(),
        sourceMac: appMac.bytes,
        targetMac: _zeroMac,
        chunkIdx: idx,
        offset: start,
        payload: payload,
      );
      try {
        await ble.write(
          deviceId,
          BleUuids.controlService,
          BleUuids.fwChunk,
          frame,
          withoutResponse: true,
        );
      } catch (e) {
        _finish(FirmwareOtaResult(
          success: false,
          message: 'Chunk write failed at idx=$idx: $e',
        ));
        return;
      }
      // Only advance if the recv path didn't rewind us mid-await. If
      // an onReq landed during the write, _nextChunkIdx may be lower
      // than (idx + 1); honor that on the next iteration.
      if (_nextChunkIdx == idx) {
        _nextChunkIdx = idx + 1;
        _chunksSent++;
        if (onProgress != null) onProgress!(_chunksSent, _totalChunks);
      }
      // Pace. Without this gap the BLE radio backs up on the receiving
      // side; tested on hardware in Phase 5a. 10 ms = ~100 chunks/sec
      // ceiling, well above the ~50 chunks/sec floor a clean session
      // hits in practice once the receiver's flash IO settles.
      if (chunkSpacingMs > 0) {
        await Future<void>.delayed(Duration(milliseconds: chunkSpacingMs));
      }
    }
    if (!_streamingActive) return;
    if (_completer == null || _completer!.isCompleted) return;

    // All chunks sent → write DONE.
    _doneSeq = _nextSeq();
    final done = buildFwDone(
      seq: _doneSeq,
      sourceMac: appMac.bytes,
      targetMac: _zeroMac,
      version: version,
      totalLen: totalLen,
      sha256Prefix: sha256Prefix,
      footerLen: footerLen,
    );
    try {
      await ble.write(
        deviceId,
        BleUuids.controlService,
        BleUuids.fwControl,
        done,
      );
    } catch (e) {
      _finish(FirmwareOtaResult(
        success: false,
        message: 'DONE write failed: $e',
      ));
      return;
    }

    // RESULT timeout. The lamp's verify + esp_ota_set_boot_partition
    // path takes a few seconds on a healthy image; budget 40s.
    Future.delayed(resultTimeout, () {
      if (_completer != null && !_completer!.isCompleted) {
        _finish(const FirmwareOtaResult(
          success: false,
          message: 'Lamp did not report an OTA result within the budget.',
        ));
      }
    });
  }

  FirmwareOtaResult _mapResultToOtaResult(ParsedFwResult res) {
    switch (res.status) {
      case FwResultStatus.success:
        return FirmwareOtaResult(
          success: true,
          message: 'Update installed; lamp is rebooting.',
          lampReportedVersion: res.version,
        );
      case FwResultStatus.signatureFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp rejected the firmware signature.',
        );
      case FwResultStatus.versionMismatch:
        return const FirmwareOtaResult(
          success: false,
          message: 'OFFER + DONE version mismatch.',
        );
      case FwResultStatus.partitionWriteFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp could not write the firmware partition.',
        );
      case FwResultStatus.partitionReadFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp could not read back the firmware partition.',
        );
      case FwResultStatus.otaBeginFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp could not begin OTA write.',
        );
      case FwResultStatus.otaEndFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp could not finalize OTA write.',
        );
      case FwResultStatus.setBootFail:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp could not set the new boot partition.',
        );
      case FwResultStatus.offerShaMismatch:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp computed a different image fingerprint.',
        );
      case FwResultStatus.unknown:
        return const FirmwareOtaResult(
          success: false,
          message: 'Lamp reported an unknown OTA failure.',
        );
    }
  }

  void _finish(FirmwareOtaResult result) {
    _streamingActive = false;
    if (_completer != null && !_completer!.isCompleted) {
      _completer!.complete(result);
    }
    unawaited(_notifySub?.cancel());
    _notifySub = null;
  }

  int _nextSeq() {
    // Bound by uint16. seqCounter_ on the lamp side wraps the same way.
    _chunkSeq = (_chunkSeq + 1) & 0xFFFF;
    return _chunkSeq;
  }

  /// Cancel an in-flight session. Idempotent; safe to call from a
  /// disposed Notifier. Does NOT send a cancel frame to the lamp — the
  /// receiver's stall-watchdog cleans up after ~chunkResendMs of no
  /// progress.
  void cancel() {
    _finish(const FirmwareOtaResult(
      success: false,
      message: 'Update cancelled.',
    ));
  }
}

/// The app's "MAC" — a 6-byte source identifier used in the OFFER/CHUNK
/// frames. The lamp's BLE OTA path doesn't authenticate against this
/// (the BLE connection handle is the real source identity), but the
/// frame format demands SOMETHING here. We bake a constant prefix so
/// logs on the lamp clearly mark which session was BLE-initiated vs
/// gossip-mesh-initiated.
class AppSourceMac {
  const AppSourceMac();
  Uint8List get bytes => Uint8List.fromList(const [
        // "APP" + 3 zero bytes. The first byte (0x41) is decoded as
        // locally-administered + unicast per IEEE 802 MAC rules — never
        // collides with a real ESP32 station MAC.
        0x41, 0x50, 0x50, 0x00, 0x00, 0x00,
      ]);
}
