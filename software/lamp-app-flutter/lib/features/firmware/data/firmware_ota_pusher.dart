import 'dart:async';
import 'dart:math';
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
  /// to reboot. Caller should NOT attempt to keep the BLE connection.
  /// The lamp drops it within ~100 ms.
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
    required this.digest,
    required this.signature,
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
  final Uint8List digest;
  final Uint8List signature;
  final int totalLen;
  final int footerLen;
  final AppSourceMac appMac;
  final ChunkProgressCallback? onProgress;
  final int chunkSpacingMs;
  final Duration acceptTimeout;
  final Duration resultTimeout;

  // Post-DONE recovery cap. A receiver that keeps REQ-ing missing chunks
  // after every DONE is either on a hopeless link or replaying; bound the
  // re-stream + re-DONE passes so a bad session can't loop forever.
  static const int _maxRecoveryRounds = 5;

  Completer<FirmwareOtaResult>? _completer;
  StreamSubscription<Uint8List>? _notifySub;
  bool _streamingActive = false;
  bool _loopRunning = false;
  bool _accepted = false;
  int _offerSeq = 0;
  // Random seq start so the first _nextSeq() yields a nonzero offerSeq
  // (1..0xFFFF) unique per session: RESULT carries no offerSeq on the wire,
  // and a stale prior-session ACCEPT must fail the offerSeq correlation.
  int _chunkSeq = Random().nextInt(0xFFFF);
  int _doneSeq  = 0;
  int _nextChunkIdx = 0;
  // Exclusive upper bound for the current send run. Normally _totalChunks; a
  // REQ narrows it to the requested window.
  int _streamEndIdx = 0;
  // Forward high-water to jump back to once a bounded rewind is served; 0 = no
  // pending resume.
  int _resumeIdx = 0;
  // Monotonic forward progress (highest chunk index reached), so a REQ rewind
  // re-sending chunks can't push the reported count past _totalChunks.
  int _chunksSent   = 0;
  int _recoveryRounds = 0;
  // Invalidates a superseded RESULT-timeout watchdog: each DONE bumps it, so a
  // recovery pass's re-DONE cancels the prior pass's pending timeout.
  int _resultTimeoutGen = 0;
  late final int _totalChunks;
  // Sized to the negotiated BLE MTU at start(); rides the OFFER's chunkSize
  // field, so the receiver's offset check (chunkIdx * offerChunkSize_) stays
  // in lockstep with the slicing below.
  late final int _fwChunkSize;
  // Target MAC. The lamp dedups frames by (sourceMac, targetMac); the
  // sender (the app) doesn't know the lamp's MAC up front. The BLE
  // path on the lamp uses the BLE connection handle for routing rather
  // than the targetMac field, so zeros here don't affect correctness.
  static final Uint8List _zeroMac = Uint8List(6);

  /// Start the OTA push. Returns a future that completes when the lamp
  /// reports a terminal status. Throws if a session is already running.
  Future<FirmwareOtaResult> start() async {
    if (_completer != null) {
      throw StateError('FirmwareOtaPusher already running');
    }
    _completer = Completer<FirmwareOtaResult>();

    // Subscribe BEFORE the OFFER write so a near-instant ACCEPT can't
    // race the write. Notify-only: a replayed cached frame from a prior
    // session would complete this one before a byte is sent.
    try {
      _fwChunkSize = chooseFwChunkSize(ble.mtu(deviceId));
      _totalChunks = (signedImage.length + _fwChunkSize - 1) ~/ _fwChunkSize;
      _notifySub = ble
          .subscribeNotifyOnly(
              deviceId, BleUuids.controlService, BleUuids.fwControl)
          .listen(_onNotify, onError: _onNotifyError);

      _offerSeq = _nextSeq();
      final offer = buildFwOffer(
        seq: _offerSeq,
        sourceMac: appMac.bytes,
        targetMac: _zeroMac,
        version: version,
        totalLen: totalLen,
        chunkSize: _fwChunkSize,
        channel: channel,
        footerLen: footerLen,
        totalChunks: _totalChunks,
        digest: digest,
        signature: signature,
      );
      // The authenticated OFFER is 152 bytes, past MTU-3 on a low-MTU link.
      await ble.write(
        deviceId,
        BleUuids.controlService,
        BleUuids.fwControl,
        offer,
        allowLongWrite: true,
      );

      // ACCEPT timeout watchdog. Holds the completer; a stuck
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
    // REQ and RESULT carry no offerSeq on the wire, so they can't be
    // correlated to this session; trust them only after this session's
    // OFFER was ACCEPTed.
    if (t != msgFwAccept && !_accepted) return;
    if (t == msgFwAccept) {
      final a = parseFwAccept(data);
      if (a == null || a.offerSeq != _offerSeq) return;
      switch (a.status) {
        case FwAcceptStatus.accept:
          // A replayed/duplicate ACCEPT must not spin up a second concurrent
          // streaming loop over the same session.
          if (_accepted) break;
          _accepted = true;
          _streamingActive = true;
          _streamEndIdx = _totalChunks;
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
        case FwAcceptStatus.declineUnverified:
          _finish(const FirmwareOtaResult(
            success: false,
            message: 'Lamp rejected the firmware signature at offer time.',
          ));
          break;
      }
    } else if (t == msgFwReq) {
      final r = parseFwReq(data);
      if (r == null) return;
      _onReq(r);
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

  // REQ handling. While a loop is running, bound the resend to
  // [firstChunkIdx, firstChunkIdx+count) and resume forward progress after;
  // once the loop has exited (post-DONE), spin up a recovery pass that
  // streams exactly that run, re-sends DONE, and exits. Recovery rounds are
  // capped so a hopeless link can't loop forever.
  void _onReq(ParsedFwReq r) {
    if (!_accepted || !_streamingActive) return;
    final first = r.firstChunkIdx;
    if (first >= _totalChunks) return;
    final count = r.chunkCount <= 0 ? 1 : r.chunkCount;
    final last = min(first + count, _totalChunks);

    if (_loopRunning) {
      // Ahead of the requested window: save the forward position, serve the
      // window, then jump back to it. Behind or inside it: a plain rewind, the
      // forward drain covers the rest.
      if (_resumeIdx == 0 && _nextChunkIdx > last) {
        _resumeIdx = _nextChunkIdx;
        _streamEndIdx = last;
      } else if (_resumeIdx != 0 && last > _streamEndIdx) {
        _streamEndIdx = last;
      }
      _nextChunkIdx = first;
      return;
    }

    // Post-DONE: no loop is running to pick up the rewind, so relaunch one
    // bounded to the requested window.
    if (_recoveryRounds >= _maxRecoveryRounds) {
      _finish(const FirmwareOtaResult(
        success: false,
        message: 'Lamp kept requesting chunks after the update finished.',
      ));
      return;
    }
    _recoveryRounds++;
    _nextChunkIdx = first;
    _streamEndIdx = last;
    _resumeIdx = 0;
    unawaited(_runStreamingLoop());
  }

  Future<void> _runStreamingLoop() async {
    _loopRunning = true;
    try {
      while (_streamingActive &&
             _completer != null && !_completer!.isCompleted) {
        if (_nextChunkIdx >= _streamEndIdx) {
          if (_resumeIdx != 0) {
            // Bounded window served; jump back to forward progress.
            _nextChunkIdx = _resumeIdx;
            _streamEndIdx = _totalChunks;
            _resumeIdx = 0;
            continue;
          }
          break;
        }
        final idx = _nextChunkIdx;
        final start = idx * _fwChunkSize;
        final end = (start + _fwChunkSize) < signedImage.length
            ? (start + _fwChunkSize)
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
        // Only advance if the recv path didn't rewind the cursor mid-await.
        // If an onReq landed during the write, _nextChunkIdx may be lower
        // than (idx + 1); honor that on the next iteration.
        if (_nextChunkIdx == idx) {
          _nextChunkIdx = idx + 1;
          if (_nextChunkIdx > _chunksSent) {
            _chunksSent = _nextChunkIdx;
            if (onProgress != null) onProgress!(_chunksSent, _totalChunks);
          }
        }
        // Pace. Without this gap the BLE radio backs up on the receiving
        // side. 10 ms = ~100 chunks/sec ceiling, above the ~50 chunks/sec
        // floor a clean session hits once the receiver's flash IO settles.
        if (chunkSpacingMs > 0) {
          await Future<void>.delayed(Duration(milliseconds: chunkSpacingMs));
        }
      }
      if (!_streamingActive) return;
      if (_completer == null || _completer!.isCompleted) return;

      // Requested run drained → write DONE.
      _doneSeq = _nextSeq();
      final done = buildFwDone(
        seq: _doneSeq,
        sourceMac: appMac.bytes,
        targetMac: _zeroMac,
        version: version,
        totalLen: totalLen,
        sha256Prefix: Uint8List.sublistView(digest, 0, fwSha256PrefixLen),
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
      // path takes a few seconds on a healthy image; budget 40s. A recovery
      // pass's re-DONE bumps the generation so this timeout is superseded.
      final gen = ++_resultTimeoutGen;
      Future.delayed(resultTimeout, () {
        if (gen != _resultTimeoutGen) return;
        if (_completer != null && !_completer!.isCompleted) {
          _finish(const FirmwareOtaResult(
            success: false,
            message: 'Lamp did not report an OTA result within the budget.',
          ));
        }
      });
    } finally {
      _loopRunning = false;
    }
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
  /// disposed Notifier. Does NOT send a cancel frame to the lamp. The
  /// receiver's stall-watchdog cleans up after ~chunkResendMs of no
  /// progress.
  void cancel() {
    _finish(const FirmwareOtaResult(
      success: false,
      message: 'Update cancelled.',
    ));
  }
}

/// The app's "MAC": a 6-byte source identifier used in the OFFER/CHUNK
/// frames. The lamp's BLE OTA path doesn't authenticate against this
/// (the BLE connection handle is the real source identity), but the
/// frame format demands SOMETHING here. Bakes in a constant prefix so
/// logs on the lamp clearly mark which session was BLE-initiated vs
/// gossip-mesh-initiated.
class AppSourceMac {
  const AppSourceMac();
  Uint8List get bytes => Uint8List.fromList(const [
        // "APP" + 3 zero bytes. The first byte (0x41) is decoded as
        // locally-administered + unicast per IEEE 802 MAC rules. Never
        // collides with a real ESP32 station MAC.
        0x41, 0x50, 0x50, 0x00, 0x00, 0x00,
      ]);
}
