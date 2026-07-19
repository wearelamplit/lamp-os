// OTA pusher state-machine tests. Drives the pusher with a small
// notify-injectable BleClient fake and verifies the OFFER/CHUNK*/DONE
// write pattern + the recv → state-pivot handling for ACCEPT, REQ,
// and RESULT.

import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/firmware/data/firmware_ota_pusher.dart';
import 'package:lamp_app/features/firmware/domain/firmware_protocol.dart';

class RecordedWrite {
  RecordedWrite(this.serviceUuid, this.charUuid, this.value,
      {required this.withoutResponse});
  final String serviceUuid;
  final String charUuid;
  final Uint8List value;
  final bool withoutResponse;
}

/// InMemoryBleClient-style fake with notify injection + write capture.
class NotifyableBle implements BleClient {
  final Set<String> _connected = {};
  final Map<String, StreamController<Uint8List>> _streams = {};
  final Map<String, Uint8List> _lastValue = {};
  final List<RecordedWrite> writes = [];

  /// Test-settable negotiated MTU. 0 (default) = unknown.
  int mtuValue = 0;

  String _key(String d, String s, String c) => '$d|$s|$c';

  StreamController<Uint8List> _ensure(String d, String s, String c) {
    return _streams.putIfAbsent(
      _key(d, s, c),
      () => StreamController<Uint8List>.broadcast(),
    );
  }

  /// Inject `bytes` as a notification on `(d, s, c)`. Yields one event
  /// loop turn so the pusher's listener can fire before the test
  /// continues.
  Future<void> simulateNotify(
      String d, String s, String c, Uint8List bytes) async {
    _lastValue[_key(d, s, c)] = bytes;
    _ensure(d, s, c).add(bytes);
    await Future<void>.delayed(Duration.zero);
  }

  /// Seed the replay cache without emitting — models fbp's static last-value
  /// cache surviving from a previous session.
  void seedLastValue(String d, String s, String c, Uint8List bytes) {
    _lastValue[_key(d, s, c)] = bytes;
  }

  @override
  Future<void> prewarm(String deviceId) async {}

  @override
  Future<void> connect(String deviceId) async {
    _connected.add(deviceId);
  }

  @override
  Future<void> disconnect(String deviceId) async {
    _connected.remove(deviceId);
  }

  @override
  bool isConnected(String deviceId) => _connected.contains(deviceId);

  @override
  int mtu(String deviceId) => mtuValue;

  @override
  Future<Uint8List> read(String d, String s, String c) async =>
      throw UnimplementedError();

  @override
  Future<Uint8List> readSection(String deviceId, String name) async =>
      throw UnimplementedError();

  @override
  Future<void> write(String d, String s, String c, Uint8List v,
      {bool withoutResponse = false, bool allowLongWrite = false}) async {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    writes.add(RecordedWrite(s, c, v, withoutResponse: withoutResponse));
  }

  // Mimics fbp's lastValueStream: replays the cached value on listen.
  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    final cached = _lastValue[_key(d, s, c)];
    final live = _ensure(d, s, c).stream;
    if (cached == null) return live;
    return () async* {
      yield cached;
      yield* live;
    }();
  }

  @override
  Stream<Uint8List> subscribeNotifyOnly(String d, String s, String c) {
    if (!_connected.contains(d)) throw BleNotConnected(d);
    return _ensure(d, s, c).stream;
  }

  @override
  Stream<bool> watchConnected(String deviceId) =>
      throw UnimplementedError();

  @override
  Future<void> cycleAdapter(String deviceId) async {}
}

// Hand-build a synthetic ACCEPT frame.
Uint8List makeAccept({
  required int seq,
  required int offerSeq,
  required int version,
  required FwAcceptStatus status,
}) {
  final buf = Uint8List(fwAcceptFixedSize);
  final view = ByteData.view(buf.buffer);
  view.setUint8(0, 0x4C);
  view.setUint8(1, 0x4D);
  view.setUint8(2, protocolVersion);
  view.setUint8(3, msgFwAccept);
  view.setUint16(4, seq, Endian.little);
  view.setUint16(18, offerSeq, Endian.little);
  view.setUint32(20, version, Endian.little);
  view.setUint8(24, status.index);
  return buf;
}

Uint8List makeReq({required int seq, required int firstIdx, required int count}) {
  final buf = Uint8List(fwReqFixedSize);
  final view = ByteData.view(buf.buffer);
  view.setUint8(0, 0x4C);
  view.setUint8(1, 0x4D);
  view.setUint8(2, protocolVersion);
  view.setUint8(3, msgFwReq);
  view.setUint16(4, seq, Endian.little);
  view.setUint16(18, firstIdx, Endian.little);
  view.setUint16(20, count, Endian.little);
  return buf;
}

Uint8List makeResult({required int seq, required FwResultStatus status, required int version}) {
  final buf = Uint8List(fwResultFixedSize);
  final view = ByteData.view(buf.buffer);
  view.setUint8(0, 0x4C);
  view.setUint8(1, 0x4D);
  view.setUint8(2, protocolVersion);
  view.setUint8(3, msgFwResult);
  view.setUint16(4, seq, Endian.little);
  view.setUint8(18, status.index);
  view.setUint32(20, version, Endian.little);
  return buf;
}

// Tiny synthetic signed image: a few chunks worth of bytes + 96-byte
// LSIG-shaped footer (the pusher doesn't inspect the footer — that's
// the verifier's job — so any 96 trailing bytes are fine).
Uint8List makeSignedImage({int signedRegion = 600}) {
  final out = Uint8List(signedRegion + 96);
  for (var i = 0; i < signedRegion; ++i) {
    out[i] = i & 0xFF;
  }
  return out;
}

int chunkWriteCount(NotifyableBle ble, int chunkIdx) {
  return ble.writes.where((w) {
    if (w.charUuid != BleUuids.fwChunk) return false;
    final view = ByteData.view(w.value.buffer, w.value.offsetInBytes);
    return view.getUint16(18, Endian.little) == chunkIdx;
  }).length;
}

int doneWriteCount(NotifyableBle ble) =>
    ble.writes.where((w) => inspectHeader(w.value) == msgFwDone).length;

FirmwareOtaPusher makePusher(NotifyableBle ble, Uint8List image) {
  return FirmwareOtaPusher(
    ble: ble,
    deviceId: 'lamp-A',
    signedImage: image,
    version: 0x00010205,
    channel: 'stable',
    digest: Uint8List(32),
    signature: Uint8List(64),
    totalLen: image.length,
    footerLen: 96,
    chunkSpacingMs: 0, // tests don't pace
    acceptTimeout: const Duration(seconds: 5),
    resultTimeout: const Duration(seconds: 5),
  );
}

void main() {
  test('sends OFFER on start; ACCEPT → streams chunks → DONE → RESULT(success)', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');

    // Image fits in 3 chunks: 3 × 200 = 600 bytes signed region.
    final image = makeSignedImage(signedRegion: 600);
    final pusher = makePusher(ble, image);
    final pendingResult = pusher.start();
    await Future<void>.delayed(Duration.zero);

    // OFFER was written to fwControl.
    final offerWrite = ble.writes.first;
    expect(offerWrite.charUuid, equals(BleUuids.fwControl));
    expect(offerWrite.withoutResponse, isFalse);
    expect(inspectHeader(offerWrite.value), equals(msgFwOffer));

    // Pluck the offer seq off the frame so ACCEPT correlates correctly.
    final offerSeq =
        ByteData.view(offerWrite.value.buffer).getUint16(4, Endian.little);

    // Simulate ACCEPT.
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 0xAA01,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.accept),
    );

    // Give the streaming loop time to drain.
    await Future<void>.delayed(const Duration(milliseconds: 50));

    // Verify CHUNKs went out on fwChunk with write-no-response.
    final chunkWrites =
        ble.writes.where((w) => w.charUuid == BleUuids.fwChunk).toList();
    expect(chunkWrites.length, equals(image.length ~/ fwChunkSize + ((image.length % fwChunkSize == 0) ? 0 : 1)));
    expect(chunkWrites.every((w) => w.withoutResponse), isTrue);
    expect(chunkWrites.first.value[3], equals(msgFwChunk));

    // Verify the DONE write was queued (last write on fwControl).
    final doneWrite = ble.writes.lastWhere((w) => w.charUuid == BleUuids.fwControl);
    expect(inspectHeader(doneWrite.value), equals(msgFwDone));

    // Simulate RESULT(success).
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 0xBB01, status: FwResultStatus.success, version: 0x00010205),
    );

    final result = await pendingResult;
    expect(result.success, isTrue);
    expect(result.lampReportedVersion, equals(0x00010205));
  });

  test('sizes chunks to a negotiated MTU; OFFER chunkSize matches the slicing', () async {
    final ble = NotifyableBle();
    ble.mtuValue = 512; // chooseFwChunkSize(512) == 483
    await ble.connect('lamp-A');

    final image = makeSignedImage(signedRegion: 1000); // 3 chunks @ 483
    final pusher = makePusher(ble, image);
    final pendingResult = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerWrite = ble.writes.first;
    final offerView = ByteData.view(offerWrite.value.buffer);
    expect(offerView.getUint16(fwOfferOffChunkSize, Endian.little), equals(483));
    final offerSeq = offerView.getUint16(4, Endian.little);

    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 0xAA02,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));

    final chunkWrites =
        ble.writes.where((w) => w.charUuid == BleUuids.fwChunk).toList();
    expect(chunkWrites.length, equals(3));
    expect(chunkWrites[0].value.length, equals(fwChunkFixedSize + 483));
    expect(chunkWrites[1].value.length, equals(fwChunkFixedSize + 483));
    expect(chunkWrites[2].value.length, equals(fwChunkFixedSize + (image.length - 2 * 483)));

    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 0xBB02, status: FwResultStatus.success, version: 0x00010205),
    );
    final result = await pendingResult;
    expect(result.success, isTrue);
  });

  test('ACCEPT(DeclineBusy) ends with success=false + busy message', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');

    final image = makeSignedImage(signedRegion: 200);
    final pusher = makePusher(ble, image);
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1, offerSeq: offerSeq, version: 0x00010205, status: FwAcceptStatus.declineBusy),
    );

    final result = await pending;
    expect(result.success, isFalse);
    expect(result.message, contains('Another update'));
  });

  test('ACCEPT(DeclineUnverified) ends with success=false + signature-rejected message', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');

    final image = makeSignedImage(signedRegion: 200);
    final pusher = makePusher(ble, image);
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.declineUnverified),
    );

    final result = await pending;
    expect(result.success, isFalse);
    expect(result.message, contains('signature'));
  });

  test('ACCEPT(DeclineAlreadyCurrent) reports success=true + already-current message', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);
    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.declineAlreadyCurrent),
    );
    final result = await pending;
    expect(result.success, isTrue);
    expect(result.message, contains('already running'));
  });

  test('REQ rewinds chunk cursor mid-stream', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    // 4 chunks worth so we can see a rewind effect.
    final image = makeSignedImage(signedRegion: 800);
    // Slow the chunk pacing so the REQ has time to land mid-stream.
    final pusher = FirmwareOtaPusher(
      ble: ble,
      deviceId: 'lamp-A',
      signedImage: image,
      version: 0x00010205,
      channel: 'stable',
      digest: Uint8List(32),
      signature: Uint8List(64),
      totalLen: image.length,
      footerLen: 96,
      chunkSpacingMs: 20,
      acceptTimeout: const Duration(seconds: 5),
      resultTimeout: const Duration(seconds: 5),
    );
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.accept),
    );

    // Let the first chunk land + the loop sleep before the rewind.
    await Future<void>.delayed(const Duration(milliseconds: 25));

    // Rewind to chunk 0. After the rewind, the loop should re-emit
    // chunk 0 → chunk0Writes.length >= 2.
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeReq(seq: 99, firstIdx: 0, count: 1),
    );

    // Drain — 4 chunks × 20 ms = 80 ms min, double it.
    await Future<void>.delayed(const Duration(milliseconds: 200));

    // RESULT
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 100, status: FwResultStatus.success, version: 0x00010205),
    );
    final result = await pending;
    expect(result.success, isTrue);

    // We saw chunkIdx=0 written at least twice (initial + after rewind).
    final chunk0Writes = ble.writes.where((w) {
      if (w.charUuid != BleUuids.fwChunk) return false;
      final view = ByteData.view(w.value.buffer, w.value.offsetInBytes);
      return view.getUint16(18, Endian.little) == 0;
    }).toList();
    expect(chunk0Writes.length, greaterThanOrEqualTo(2));
  });

  test('RESULT(signatureFail) surfaces lamp-rejected message', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);
    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1, offerSeq: offerSeq, version: 0x00010205, status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 30));
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 2, status: FwResultStatus.signatureFail, version: 0),
    );
    final result = await pending;
    expect(result.success, isFalse);
    expect(result.message, contains('signature'));
  });

  test('stale cached RESULT replayed at subscribe time does not complete the session', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    // A previous session's RESULT(success) sits in the platform's
    // last-value cache when the second pusher subscribes.
    ble.seedLastValue(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 7, status: FwResultStatus.success, version: 0x00010204),
    );

    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    var completed = false;
    final pending = pusher.start();
    unawaited(pending.whenComplete(() => completed = true));
    await Future<void>.delayed(const Duration(milliseconds: 50));

    expect(completed, isFalse,
        reason: 'replayed prior-session RESULT must not finish a fresh session');
    pusher.cancel();
    await pending;
  });

  test('RESULT notify before ACCEPT is ignored; session still completes normally', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    var completed = false;
    final pending = pusher.start();
    unawaited(pending.whenComplete(() => completed = true));
    await Future<void>.delayed(Duration.zero);

    // Stale RESULT arrives before any ACCEPT — uncorrelatable, must be dropped.
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 7, status: FwResultStatus.success, version: 0x00010204),
    );
    expect(completed, isFalse);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1,
          offerSeq: offerSeq,
          version: 0x00010205,
          status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 30));
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 8, status: FwResultStatus.success, version: 0x00010205),
    );
    final result = await pending;
    expect(result.success, isTrue);
    expect(result.lampReportedVersion, equals(0x00010205));
  });

  test('ACCEPT with mismatched offerSeq is ignored; offerSeq is nonzero', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    expect(offerSeq, isNot(0));

    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1,
          offerSeq: (offerSeq + 1) & 0xFFFF,
          version: 0x00010205,
          status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 30));
    expect(ble.writes.where((w) => w.charUuid == BleUuids.fwChunk), isEmpty,
        reason: 'a prior-session ACCEPT must not start streaming');
    pusher.cancel();
    await pending;
  });

  test('stale REQ after DONE re-streams the run and re-sends DONE', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final image = makeSignedImage(signedRegion: 600);
    final pusher = makePusher(ble, image);
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1, offerSeq: offerSeq, version: 0x00010205, status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));

    expect(doneWriteCount(ble), equals(1));
    final chunk1Before = chunkWriteCount(ble, 1);

    // Receiver missed chunk 1; REQs it after DONE (loop already exited).
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeReq(seq: 50, firstIdx: 1, count: 1),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));

    expect(chunkWriteCount(ble, 1), greaterThan(chunk1Before),
        reason: 'chunk 1 must re-stream on a post-DONE REQ');
    expect(doneWriteCount(ble), equals(2),
        reason: 'recovery pass must re-send DONE');

    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 60, status: FwResultStatus.success, version: 0x00010205),
    );
    final result = await pending;
    expect(result.success, isTrue);
  });

  test('post-DONE REQ recovery is capped', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 600));
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);
    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
          seq: 1, offerSeq: offerSeq, version: 0x00010205, status: FwAcceptStatus.accept),
    );
    await Future<void>.delayed(const Duration(milliseconds: 50));

    // Six post-DONE REQs. Five recovery passes run; the sixth is over budget
    // and fails the session instead of looping forever.
    for (var i = 0; i < 6; ++i) {
      await ble.simulateNotify(
        'lamp-A',
        BleUuids.controlService,
        BleUuids.fwControl,
        makeReq(seq: 50 + i, firstIdx: 1, count: 1),
      );
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }

    final result = await pending;
    expect(result.success, isFalse);
    expect(result.message, contains('after the update finished'));
  });

  test('replayed ACCEPT does not start a second streaming loop', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final image = makeSignedImage(signedRegion: 600);
    final expectedChunks = (image.length + fwChunkSize - 1) ~/ fwChunkSize;
    final pusher = makePusher(ble, image);
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);

    final offerSeq = ByteData.view(ble.writes.first.value.buffer)
        .getUint16(4, Endian.little);
    final accept = makeAccept(
        seq: 1, offerSeq: offerSeq, version: 0x00010205, status: FwAcceptStatus.accept);
    await ble.simulateNotify(
        'lamp-A', BleUuids.controlService, BleUuids.fwControl, accept);
    await Future<void>.delayed(const Duration(milliseconds: 50));

    final chunksAfterFirst =
        ble.writes.where((w) => w.charUuid == BleUuids.fwChunk).length;
    expect(chunksAfterFirst, equals(expectedChunks));

    // A duplicate/replayed ACCEPT with the same offerSeq must be ignored.
    await ble.simulateNotify(
        'lamp-A', BleUuids.controlService, BleUuids.fwControl, accept);
    await Future<void>.delayed(const Duration(milliseconds: 50));

    expect(ble.writes.where((w) => w.charUuid == BleUuids.fwChunk).length,
        equals(chunksAfterFirst),
        reason: 'a replayed ACCEPT must not re-stream the image');

    await ble.simulateNotify(
      'lamp-A',
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 9, status: FwResultStatus.success, version: 0x00010205),
    );
    final result = await pending;
    expect(result.success, isTrue);
  });

  test('cancel() ends the future with a cancelled message', () async {
    final ble = NotifyableBle();
    await ble.connect('lamp-A');
    final pusher = makePusher(ble, makeSignedImage(signedRegion: 200));
    final pending = pusher.start();
    await Future<void>.delayed(Duration.zero);
    pusher.cancel();
    final result = await pending;
    expect(result.success, isFalse);
    expect(result.message, contains('cancel'));
  });
}
