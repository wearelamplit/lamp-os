// Dart mirror of the MSG_FW_* wire format from
// software/lamp-os/src/components/network/lamp_protocol.hpp. Keep field
// orderings + LE-encoding in lockstep with the lamp side. A byte slip
// here is a silent-drop in the receiver's parser, which is the
// canonical example of "the code wins ties; update docs/dev/networking.md
// when it doesn't."
//
// The lamp-side BLE OTA path accepts the SAME wire format as the ESP-NOW mesh
// path (no envelope, no re-wrapping), so these frames can be hand-built in
// Dart and written to CHAR_FW_CONTROL / CHAR_FW_CHUNK without a native bridge.

import 'dart:typed_data';

/// Header constants. Must match `LM` magic + `PROTOCOL_VERSION = 0x04`
/// in the C++ side. The lamp's `inspect()` early-rejects any frame whose
/// byte 2 != PROTOCOL_VERSION before reading any fields; emitting the
/// wrong version here makes every app-pushed OFFER a silent drop.
const int magic0 = 0x4C; // 'L'
const int magic1 = 0x4D; // 'M'
const int protocolVersion = 0x04;

const int msgFwOffer  = 0x40;
const int msgFwAccept = 0x41;
const int msgFwChunk  = 0x42;
const int msgFwReq    = 0x43;
const int msgFwDone   = 0x44;
const int msgFwResult = 0x45;

// Fixed sizes. Same values as the static_asserts on the lamp side.
const int fwOfferFixedSize  = 56;
const int fwAcceptFixedSize = 28;
const int fwChunkFixedSize  = 26;
const int fwReqFixedSize    = 24;
const int fwDoneFixedSize   = 38;
const int fwResultFixedSize = 24;

/// Baseline chunk payload size: the floor every link can carry, and the
/// fallback when the negotiated MTU is unknown or too small to beat it.
const int fwChunkSize = 200;

/// Ceiling chunk payload size for app-pushed OTA over BLE, bounded by the
/// practical ATT MTU (real devices land well below this). Unrelated to the
/// lamp-to-lamp ESP-NOW chunk size; the lamp's OTA receiver accepts up to its
/// own larger max, so this app-side cap always fits within it.
const int fwChunkSizeMax = 768;

/// Picks the OTA chunk payload size for a session from the negotiated BLE
/// ATT MTU (pass 0 if unknown). Sized to fill the MTU minus the 3-byte ATT
/// write overhead and the chunk frame header, capped at [fwChunkSizeMax]
/// and floored at [fwChunkSize] so a low or unreported MTU still works —
/// that floor is the size every link supports today.
///
/// Reads the negotiated MTU despite flutter_blue_plus not always surfacing
/// it reliably (see the config-page chunk size on the firmware side, which
/// pins a constant for that reason): here the OFFER's chunkSize and the
/// slicing both derive from this same value, so a stale or low reading just
/// yields a slower or failed transfer, never a misframe. The config-page
/// protocol's "short chunk = done" framing can't tolerate that ambiguity.
int chooseFwChunkSize(int negotiatedMtu) {
  if (negotiatedMtu <= 0) return fwChunkSize;
  final sized = negotiatedMtu - 3 - fwChunkFixedSize;
  final capped = sized > fwChunkSizeMax ? fwChunkSizeMax : sized;
  return capped < fwChunkSize ? fwChunkSize : capped;
}

/// Zero-padded ASCII channel string width. Carries `{lampType}-{channel}`.
const int fwChannelLen = 16;

/// FW_OFFER body field offsets. Match the lamp-side `FW_OFFER_OFF_*`
/// constants. Widening any field becomes a single-line shift here.
const int fwOfferOffVersion     = 18;
const int fwOfferOffTotalLen    = 22;
const int fwOfferOffChunkSize   = 26;
const int fwOfferOffChannel     = 28;
const int fwOfferOffSha256      = fwOfferOffChannel + fwChannelLen;       // 44
const int fwOfferOffFooterLen   = fwOfferOffSha256 + 8;                   // 52  (sha256 is 8 bytes)
const int fwOfferOffTotalChunks = fwOfferOffFooterLen + 2;                // 54

/// Auth trailer, appended after the 56-byte body: full digest then the
/// ed25519 signature. The lamp returns DeclineUnverified before any chunk
/// streams unless the OFFER carries a valid trailer.
const int fwOfferOffDigest = fwOfferFixedSize;                 // 56
const int fwOfferOffSig     = fwOfferOffDigest + fwSha256FullLen; // 88
const int fwOfferAuthSize   = fwOfferOffSig + fwSigLen;        // 152

/// First 8 bytes of SHA-256(signed region): image fingerprint.
const int fwSha256PrefixLen = 8;

/// Full SHA-256(signed region) length and ed25519 signature length.
const int fwSha256FullLen = 32;
const int fwSigLen        = 64;

/// LSIG footer length in bytes (96). The signed image transmitted
/// over BLE includes the footer; the receiver strips + verifies it.
const int fwFooterLenV1 = 96;

/// ACCEPT status byte. Values match the lamp's `FwAcceptStatus` enum.
enum FwAcceptStatus {
  accept,                // 0: go ahead and stream
  declineBusy,           // 1: another OTA mid-flow
  declineAlreadyCurrent, // 2: offer.version <= mine
  declineUnverified,     // 3: offer failed the offer-time ed25519 check
}

FwAcceptStatus _acceptStatusFromByte(int b) {
  switch (b) {
    case 0: return FwAcceptStatus.accept;
    case 1: return FwAcceptStatus.declineBusy;
    case 2: return FwAcceptStatus.declineAlreadyCurrent;
    case 3: return FwAcceptStatus.declineUnverified;
    default: return FwAcceptStatus.declineBusy; // safe default: treat unknown as decline
  }
}

/// REQ reason byte. Diagnostic-only; the lamp logs it.
enum FwReqReason {
  gap,           // 0: explicit gap fill from receiver-side bitmap scan
  stallWatchdog, // 1: 2s without progress
}

FwReqReason _reqReasonFromByte(int b) {
  switch (b) {
    case 1: return FwReqReason.stallWatchdog;
    default: return FwReqReason.gap;
  }
}

/// RESULT status enum. Wisp side / app side both surface this; the lamp
/// reports it as the OTA outcome before rebooting.
enum FwResultStatus {
  success,            // 0: verified + boot partition set + rebooting
  signatureFail,      // 1
  versionMismatch,    // 2
  partitionWriteFail, // 3
  partitionReadFail,  // 4
  otaBeginFail,       // 5
  otaEndFail,         // 6
  setBootFail,        // 7
  offerShaMismatch,   // 8
  unknown,            // 9..255 reserved
}

FwResultStatus _resultStatusFromByte(int b) {
  switch (b) {
    case 0: return FwResultStatus.success;
    case 1: return FwResultStatus.signatureFail;
    case 2: return FwResultStatus.versionMismatch;
    case 3: return FwResultStatus.partitionWriteFail;
    case 4: return FwResultStatus.partitionReadFail;
    case 5: return FwResultStatus.otaBeginFail;
    case 6: return FwResultStatus.otaEndFail;
    case 7: return FwResultStatus.setBootFail;
    case 8: return FwResultStatus.offerShaMismatch;
    default: return FwResultStatus.unknown;
  }
}

void _writeHeader(ByteData out, int msgType, int seq) {
  out.setUint8(0, magic0);
  out.setUint8(1, magic1);
  out.setUint8(2, protocolVersion);
  out.setUint8(3, msgType);
  out.setUint16(4, seq, Endian.little);
}

void _writeMac(ByteData out, int offset, Uint8List mac) {
  if (mac.length != 6) {
    throw ArgumentError('MAC must be exactly 6 bytes, got ${mac.length}');
  }
  for (var i = 0; i < 6; ++i) {
    out.setUint8(offset + i, mac[i]);
  }
}

Uint8List _readMac(ByteData in_, int offset) {
  final out = Uint8List(6);
  for (var i = 0; i < 6; ++i) {
    out[i] = in_.getUint8(offset + i);
  }
  return out;
}

/// Build the authenticated MSG_FW_OFFER (152 bytes). Layout:
///   hdr(6) + src(6) + tgt(6) + version(4 LE) + totalLen(4 LE) +
///   chunkSize(2 LE) + channel(16 zero-pad) + sha256Prefix(8) +
///   footerLen(2 LE) + totalChunks(2 LE) + digest(32) + signature(64)
Uint8List buildFwOffer({
  required int seq,
  required Uint8List sourceMac,
  required Uint8List targetMac,
  required int version,
  required int totalLen,
  required int chunkSize,
  required String channel,
  required int footerLen,
  required int totalChunks,
  required Uint8List digest,
  required Uint8List signature,
}) {
  if (digest.length != fwSha256FullLen) {
    throw ArgumentError(
        'digest must be $fwSha256FullLen bytes, got ${digest.length}');
  }
  if (signature.length != fwSigLen) {
    throw ArgumentError(
        'signature must be $fwSigLen bytes, got ${signature.length}');
  }
  final bytes = Uint8List(fwOfferAuthSize);
  final view  = ByteData.view(bytes.buffer);
  _writeHeader(view, msgFwOffer, seq);
  _writeMac(view, 6, sourceMac);
  _writeMac(view, 12, targetMac);
  view.setUint32(fwOfferOffVersion, version, Endian.little);
  view.setUint32(fwOfferOffTotalLen, totalLen, Endian.little);
  view.setUint16(fwOfferOffChunkSize, chunkSize, Endian.little);
  // Channel: zero-pad to fwChannelLen bytes, truncate if longer.
  final channelBytes = channel.codeUnits;
  for (var i = 0; i < fwChannelLen; ++i) {
    view.setUint8(fwOfferOffChannel + i,
        i < channelBytes.length ? channelBytes[i] : 0);
  }
  for (var i = 0; i < fwSha256PrefixLen; ++i) {
    view.setUint8(fwOfferOffSha256 + i, digest[i]);
  }
  view.setUint16(fwOfferOffFooterLen, footerLen, Endian.little);
  view.setUint16(fwOfferOffTotalChunks, totalChunks, Endian.little);
  bytes.setRange(fwOfferOffDigest, fwOfferOffDigest + fwSha256FullLen, digest);
  bytes.setRange(fwOfferOffSig, fwOfferOffSig + fwSigLen, signature);
  return bytes;
}

/// Build MSG_FW_CHUNK (26 + payload.length bytes). Layout:
///   hdr(6) + src(6) + tgt(6) + chunkIdx(2 LE) + offset(4 LE) +
///   len(2 LE) + payload(len)
Uint8List buildFwChunk({
  required int seq,
  required Uint8List sourceMac,
  required Uint8List targetMac,
  required int chunkIdx,
  required int offset,
  required Uint8List payload,
}) {
  if (payload.isEmpty || payload.length > fwChunkSizeMax) {
    throw ArgumentError(
        'chunk payload must be 1..$fwChunkSizeMax bytes, got ${payload.length}');
  }
  final total = fwChunkFixedSize + payload.length;
  final bytes = Uint8List(total);
  final view  = ByteData.view(bytes.buffer);
  _writeHeader(view, msgFwChunk, seq);
  _writeMac(view, 6, sourceMac);
  _writeMac(view, 12, targetMac);
  view.setUint16(18, chunkIdx, Endian.little);
  view.setUint32(20, offset, Endian.little);
  view.setUint16(24, payload.length, Endian.little);
  for (var i = 0; i < payload.length; ++i) {
    bytes[fwChunkFixedSize + i] = payload[i];
  }
  return bytes;
}

/// Build MSG_FW_DONE (38 bytes). Layout:
///   hdr(6) + src(6) + tgt(6) + version(4 LE) + totalLen(4 LE) +
///   sha256Prefix(8) + footerLen(2 LE) + reserved(2)
Uint8List buildFwDone({
  required int seq,
  required Uint8List sourceMac,
  required Uint8List targetMac,
  required int version,
  required int totalLen,
  required Uint8List sha256Prefix,
  required int footerLen,
}) {
  if (sha256Prefix.length != fwSha256PrefixLen) {
    throw ArgumentError(
        'sha256Prefix must be $fwSha256PrefixLen bytes, got ${sha256Prefix.length}');
  }
  final bytes = Uint8List(fwDoneFixedSize);
  final view  = ByteData.view(bytes.buffer);
  _writeHeader(view, msgFwDone, seq);
  _writeMac(view, 6, sourceMac);
  _writeMac(view, 12, targetMac);
  view.setUint32(18, version, Endian.little);
  view.setUint32(22, totalLen, Endian.little);
  for (var i = 0; i < fwSha256PrefixLen; ++i) {
    view.setUint8(26 + i, sha256Prefix[i]);
  }
  view.setUint16(34, footerLen, Endian.little);
  view.setUint16(36, 0); // reserved
  return bytes;
}

/// Inspect a header. Returns the msgType byte iff this is a well-formed
/// frame for the protocol; returns null for any mismatch. Matches
/// lamp-side `lamp_protocol::inspect`.
int? inspectHeader(Uint8List data) {
  if (data.length < 6) return null;
  if (data[0] != magic0 || data[1] != magic1) return null;
  if (data[2] != protocolVersion) return null;
  return data[3];
}

/// Parsed MSG_FW_ACCEPT.
class ParsedFwAccept {
  ParsedFwAccept({
    required this.seq,
    required this.sourceMac,
    required this.targetMac,
    required this.offerSeq,
    required this.version,
    required this.status,
  });
  final int seq;
  final Uint8List sourceMac;
  final Uint8List targetMac;
  final int offerSeq;
  final int version;
  final FwAcceptStatus status;
}

ParsedFwAccept? parseFwAccept(Uint8List data) {
  if (inspectHeader(data) != msgFwAccept) return null;
  if (data.length < fwAcceptFixedSize) return null;
  final view = ByteData.view(data.buffer, data.offsetInBytes, data.length);
  return ParsedFwAccept(
    seq: view.getUint16(4, Endian.little),
    sourceMac: _readMac(view, 6),
    targetMac: _readMac(view, 12),
    offerSeq: view.getUint16(18, Endian.little),
    version: view.getUint32(20, Endian.little),
    status: _acceptStatusFromByte(view.getUint8(24)),
  );
}

/// Parsed MSG_FW_REQ.
class ParsedFwReq {
  ParsedFwReq({
    required this.seq,
    required this.sourceMac,
    required this.targetMac,
    required this.firstChunkIdx,
    required this.chunkCount,
    required this.reason,
  });
  final int seq;
  final Uint8List sourceMac;
  final Uint8List targetMac;
  final int firstChunkIdx;
  final int chunkCount;
  final FwReqReason reason;
}

ParsedFwReq? parseFwReq(Uint8List data) {
  if (inspectHeader(data) != msgFwReq) return null;
  if (data.length < fwReqFixedSize) return null;
  final view = ByteData.view(data.buffer, data.offsetInBytes, data.length);
  return ParsedFwReq(
    seq: view.getUint16(4, Endian.little),
    sourceMac: _readMac(view, 6),
    targetMac: _readMac(view, 12),
    firstChunkIdx: view.getUint16(18, Endian.little),
    chunkCount: view.getUint16(20, Endian.little),
    reason: _reqReasonFromByte(view.getUint8(22)),
  );
}

/// Parsed MSG_FW_RESULT.
class ParsedFwResult {
  ParsedFwResult({
    required this.seq,
    required this.sourceMac,
    required this.targetMac,
    required this.status,
    required this.detail,
    required this.version,
  });
  final int seq;
  final Uint8List sourceMac;
  final Uint8List targetMac;
  final FwResultStatus status;
  final int detail;
  final int version;
}

ParsedFwResult? parseFwResult(Uint8List data) {
  if (inspectHeader(data) != msgFwResult) return null;
  if (data.length < fwResultFixedSize) return null;
  final view = ByteData.view(data.buffer, data.offsetInBytes, data.length);
  return ParsedFwResult(
    seq: view.getUint16(4, Endian.little),
    sourceMac: _readMac(view, 6),
    targetMac: _readMac(view, 12),
    status: _resultStatusFromByte(view.getUint8(18)),
    detail: view.getUint8(19),
    version: view.getUint32(20, Endian.little),
  );
}
