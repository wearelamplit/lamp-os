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
  final List<RecordedWrite> writes = [];

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
    _ensure(d, s, c).add(bytes);
    await Future<void>.delayed(Duration.zero);
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

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
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

FirmwareOtaPusher makePusher(NotifyableBle ble, Uint8List image) {
  return FirmwareOtaPusher(
    ble: ble,
    deviceId: 'lamp-A',
    signedImage: image,
    version: 0x00010205,
    channel: 'stable',
    sha256Prefix: Uint8List.fromList([1, 2, 3, 4, 5, 6, 7, 8]),
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
      sha256Prefix: Uint8List.fromList([1, 2, 3, 4, 5, 6, 7, 8]),
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
