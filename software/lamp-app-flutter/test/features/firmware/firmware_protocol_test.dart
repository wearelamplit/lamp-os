// Byte-layout tests for the Dart MSG_FW_* frame builders. These verify
// the wire format matches the lamp-side `buildFwOffer` / `buildFwChunk`
// / `buildFwDone` in lamp_protocol.hpp — a byte slip here silently
// breaks BLE OTA, so the offsets are pinned to literal values.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/domain/firmware_protocol.dart';

Uint8List _macAB() => Uint8List.fromList([0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33]);
Uint8List _macFF() => Uint8List.fromList([0xFF, 0xEE, 0xDD, 0x44, 0x55, 0x66]);
Uint8List _sha8()  => Uint8List.fromList([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]);

void main() {
  // Literal version pin so a Dart-side regression that drifts off the
  // lamp's PROTOCOL_VERSION fails this test loudly. The lamp's
  // inspect() rejects any frame whose byte 2 != PROTOCOL_VERSION before
  // any field read, so a mismatch silently breaks BLE OTA otherwise.
  test('protocolVersion matches the lamp side (v0x04)', () {
    expect(protocolVersion, equals(0x04));
  });

  group('buildFwOffer', () {
    test('emits a 56-byte frame with correct header + LE encoding', () {
      final frame = buildFwOffer(
        seq: 0x1234,
        sourceMac: _macAB(),
        targetMac: _macFF(),
        version: 0x00010205, // 1.2.5
        totalLen: 0x00100000, // 1 MB
        chunkSize: 200,
        channel: 'standard-stable',
        sha256Prefix: _sha8(),
        footerLen: 96,
        totalChunks: 5243,
      );
      expect(frame.length, equals(fwOfferFixedSize));
      expect(fwOfferFixedSize, equals(56));
      // Header
      expect(frame[0], equals(0x4C)); // 'L'
      expect(frame[1], equals(0x4D)); // 'M'
      expect(frame[2], equals(protocolVersion));
      expect(frame[3], equals(msgFwOffer));
      expect(frame[4], equals(0x34)); // seq LSB
      expect(frame[5], equals(0x12)); // seq MSB
      // Source MAC
      expect(frame.sublist(6, 12), equals(_macAB()));
      // Target MAC
      expect(frame.sublist(12, 18), equals(_macFF()));
      // Version (LE)
      final v = ByteData.view(frame.buffer, frame.offsetInBytes + 18, 4);
      expect(v.getUint32(0, Endian.little), equals(0x00010205));
      // Total len (LE)
      final t = ByteData.view(frame.buffer, frame.offsetInBytes + 22, 4);
      expect(t.getUint32(0, Endian.little), equals(0x00100000));
      // Chunk size (LE)
      final c = ByteData.view(frame.buffer, frame.offsetInBytes + 26, 2);
      expect(c.getUint16(0, Endian.little), equals(200));
      // Channel — 16-byte zero-padded `standard-stable`
      expect(frame.sublist(28, 44),
          equals([...'standard-stable'.codeUnits, 0]));
      // SHA-256 prefix
      expect(frame.sublist(44, 52), equals(_sha8()));
      // Footer len (LE)
      final f = ByteData.view(frame.buffer, frame.offsetInBytes + 52, 2);
      expect(f.getUint16(0, Endian.little), equals(96));
      // Total chunks (LE)
      final tc = ByteData.view(frame.buffer, frame.offsetInBytes + 54, 2);
      expect(tc.getUint16(0, Endian.little), equals(5243));
    });

    test('truncates a >16-char channel', () {
      final frame = buildFwOffer(
        seq: 1,
        sourceMac: _macAB(),
        targetMac: _macFF(),
        version: 1,
        totalLen: 100000,
        chunkSize: 200,
        channel: 'this-channel-is-way-too-long-for-the-slot',
        sha256Prefix: _sha8(),
        footerLen: 96,
        totalChunks: 500,
      );
      // Only first 16 bytes copied; not null-terminated on the wire.
      expect(frame.sublist(28, 44),
          equals('this-channel-is-'.codeUnits));
    });

    test('rejects a wrong-sized sha256Prefix', () {
      expect(
        () => buildFwOffer(
          seq: 1,
          sourceMac: _macAB(),
          targetMac: _macFF(),
          version: 1,
          totalLen: 100000,
          chunkSize: 200,
          channel: 'stable',
          sha256Prefix: Uint8List(4),  // wrong size
          footerLen: 96,
          totalChunks: 500,
        ),
        throwsArgumentError,
      );
    });
  });

  group('buildFwChunk', () {
    test('emits 26 + payload.length bytes', () {
      final payload = Uint8List.fromList(List.generate(100, (i) => i & 0xFF));
      final frame = buildFwChunk(
        seq: 0xABCD,
        sourceMac: _macAB(),
        targetMac: _macFF(),
        chunkIdx: 42,
        offset: 8400, // 42 * 200
        payload: payload,
      );
      expect(frame.length, equals(fwChunkFixedSize + payload.length));
      expect(frame[3], equals(msgFwChunk));
      final view = ByteData.view(frame.buffer, frame.offsetInBytes);
      expect(view.getUint16(18, Endian.little), equals(42));
      expect(view.getUint32(20, Endian.little), equals(8400));
      expect(view.getUint16(24, Endian.little), equals(100));
      // Payload starts at offset 26.
      expect(frame.sublist(26, 26 + 100), equals(payload));
    });

    test('rejects empty or oversized payload', () {
      expect(
        () => buildFwChunk(
          seq: 1,
          sourceMac: _macAB(),
          targetMac: _macFF(),
          chunkIdx: 0,
          offset: 0,
          payload: Uint8List(0),
        ),
        throwsArgumentError,
      );
      expect(
        () => buildFwChunk(
          seq: 1,
          sourceMac: _macAB(),
          targetMac: _macFF(),
          chunkIdx: 0,
          offset: 0,
          payload: Uint8List(fwChunkSize + 1),
        ),
        throwsArgumentError,
      );
    });
  });

  group('buildFwDone', () {
    test('emits a 38-byte frame with correct fields', () {
      final frame = buildFwDone(
        seq: 99,
        sourceMac: _macAB(),
        targetMac: _macFF(),
        version: 0x00010205,
        totalLen: 1234567,
        sha256Prefix: _sha8(),
        footerLen: 96,
      );
      expect(frame.length, equals(fwDoneFixedSize));
      expect(frame[3], equals(msgFwDone));
      final view = ByteData.view(frame.buffer, frame.offsetInBytes);
      expect(view.getUint32(18, Endian.little), equals(0x00010205));
      expect(view.getUint32(22, Endian.little), equals(1234567));
      expect(frame.sublist(26, 34), equals(_sha8()));
      expect(view.getUint16(34, Endian.little), equals(96));
      // Reserved bytes must be zero.
      expect(frame[36], equals(0));
      expect(frame[37], equals(0));
    });
  });

  group('parsers', () {
    test('parseFwAccept reads the 28-byte layout', () {
      // Hand-build a known-good ACCEPT frame (matches the lamp's
      // buildFwAccept byte layout).
      final buf = Uint8List(fwAcceptFixedSize);
      final view = ByteData.view(buf.buffer);
      view.setUint8(0, 0x4C);
      view.setUint8(1, 0x4D);
      view.setUint8(2, protocolVersion);
      view.setUint8(3, msgFwAccept);
      view.setUint16(4, 0xBEEF, Endian.little);
      for (var i = 0; i < 6; ++i) {
        view.setUint8(6 + i, _macAB()[i]);
        view.setUint8(12 + i, _macFF()[i]);
      }
      view.setUint16(18, 0x1234, Endian.little); // offerSeq
      view.setUint32(20, 0x00010002, Endian.little); // version
      view.setUint8(24, 0); // accept
      final parsed = parseFwAccept(buf);
      expect(parsed, isNotNull);
      expect(parsed!.seq, equals(0xBEEF));
      expect(parsed.offerSeq, equals(0x1234));
      expect(parsed.version, equals(0x00010002));
      expect(parsed.status, equals(FwAcceptStatus.accept));
      expect(parsed.sourceMac, equals(_macAB()));
    });

    test('parseFwAccept rejects bad magic', () {
      final buf = Uint8List(fwAcceptFixedSize);
      buf[0] = 0xFF;
      buf[1] = 0xFF;
      expect(parseFwAccept(buf), isNull);
    });

    test('parseFwAccept rejects wrong protocol version', () {
      final buf = Uint8List(fwAcceptFixedSize);
      buf[0] = 0x4C;
      buf[1] = 0x4D;
      buf[2] = 0xFF; // wrong version
      buf[3] = msgFwAccept;
      expect(parseFwAccept(buf), isNull);
    });

    test('parseFwReq reads the 24-byte layout', () {
      final buf = Uint8List(fwReqFixedSize);
      final view = ByteData.view(buf.buffer);
      view.setUint8(0, 0x4C);
      view.setUint8(1, 0x4D);
      view.setUint8(2, protocolVersion);
      view.setUint8(3, msgFwReq);
      view.setUint16(4, 5, Endian.little);
      view.setUint16(18, 42, Endian.little); // firstChunkIdx
      view.setUint16(20, 8, Endian.little);  // chunkCount
      view.setUint8(22, 1);                   // StallWatchdog
      final parsed = parseFwReq(buf);
      expect(parsed, isNotNull);
      expect(parsed!.firstChunkIdx, equals(42));
      expect(parsed.chunkCount, equals(8));
      expect(parsed.reason, equals(FwReqReason.stallWatchdog));
    });

    test('parseFwResult reads the 24-byte layout', () {
      final buf = Uint8List(fwResultFixedSize);
      final view = ByteData.view(buf.buffer);
      view.setUint8(0, 0x4C);
      view.setUint8(1, 0x4D);
      view.setUint8(2, protocolVersion);
      view.setUint8(3, msgFwResult);
      view.setUint16(4, 1, Endian.little);
      view.setUint8(18, 0); // Success
      view.setUint8(19, 7); // detail (cosmetic)
      view.setUint32(20, 0x00010002, Endian.little);
      final parsed = parseFwResult(buf);
      expect(parsed, isNotNull);
      expect(parsed!.status, equals(FwResultStatus.success));
      expect(parsed.detail, equals(7));
      expect(parsed.version, equals(0x00010002));
    });
  });

  group('inspectHeader', () {
    test('returns the msgType byte for a valid frame', () {
      final f = buildFwDone(
        seq: 1,
        sourceMac: _macAB(),
        targetMac: _macFF(),
        version: 1,
        totalLen: 100,
        sha256Prefix: _sha8(),
        footerLen: 96,
      );
      expect(inspectHeader(f), equals(msgFwDone));
    });

    test('returns null for too-short data', () {
      expect(inspectHeader(Uint8List(5)), isNull);
    });

    test('returns null for bad magic', () {
      final buf = Uint8List(fwAcceptFixedSize);
      buf[0] = 0x00;
      buf[1] = 0x00;
      expect(inspectHeader(buf), isNull);
    });
  });
}
