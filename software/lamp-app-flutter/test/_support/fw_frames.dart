// Lamp-side firmware frames a test injects as notifies.

import 'dart:typed_data';

import 'package:lamp_app/features/firmware/domain/firmware_protocol.dart';

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
