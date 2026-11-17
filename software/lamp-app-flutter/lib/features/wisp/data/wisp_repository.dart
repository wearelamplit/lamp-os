import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import '../../control/domain/lamp_color.dart';
import '../../../core/ble/ble_client.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';
import '../domain/tuple_sampler.dart';
import '../domain/wisp_config.dart';
import '../domain/wisp_claims.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';

/// Thin wrapper around the two wisp BLE characteristics. The notifier layer
/// owns lifetime/state; this class is just IO.
///
/// When [password] is non-null and non-empty, ops are sealed via
/// LampCrypto.encryptOp (salt = CHAR_WISP_OP uuid LE, info key = "wispOp").
/// setManualPalette is always plaintext. It carries no secret.
class WispRepository {
  WispRepository(this._ble, this._deviceId, {this._password});

  final BleClient _ble;
  final String _deviceId;

  // Sealed when non-null/non-empty; plaintext otherwise.
  String? _password;

  void updatePassword(String? password) => _password = password;

  static final _wispOpSalt = uuidSaltLE16(BleUuids.wispOp);

  bool _wispSectionsSupported = false;

  /// True once the lamp has served the `wispstatus` page section, which a
  /// lamp that has it always answers with at least `"{}"`. It also serves
  /// `wispconfig`. Sticky: an empty read before the auth handshake looks
  /// the same as a lamp that predates the sections, so only a hit counts.
  bool get wispSectionsSupported => _wispSectionsSupported;

  /// Record that the lamp serves the wisp sections, from a status only such a
  /// lamp merges (`configLastSeenMs`), so a build whose reads all landed
  /// before the auth handshake does not leave the sections unused.
  void noteWispSections() => _wispSectionsSupported = true;

  /// One-shot read of the merged wispStatus JSON: whole from the
  /// `wispstatus` page section, or from CHAR_WISP_STATUS (capped at 512 B)
  /// on a lamp without it. Empty / `"{}"` / unparseable payloads map to
  /// [WispStatus.empty].
  Future<WispStatus> readStatus() async {
    Uint8List bytes;
    try {
      bytes = await _ble.readSection(_deviceId, 'wispstatus');
    } catch (_) {
      bytes = Uint8List(0);
    }
    if (bytes.isNotEmpty) {
      _wispSectionsSupported = true;
      return WispStatus.fromBytes(bytes);
    }
    bytes = await _ble.read(
      _deviceId,
      BleUuids.controlService,
      BleUuids.wispStatus,
    );
    return WispStatus.fromBytes(bytes);
  }

  /// Read the wisp's settings from the `wispconfig` page section. Null when
  /// the lamp has no sections, has no config cached yet, or the read fails;
  /// never throws.
  Future<WispConfig?> readConfig() async {
    if (!_wispSectionsSupported) return null;
    try {
      final bytes = await _ble.readSection(_deviceId, 'wispconfig');
      return WispConfig.fromBytes(bytes);
    } catch (_) {
      return null;
    }
  }

  /// Read the wisp's stored manual palette from its dedicated page section.
  /// Kept off CHAR_WISP_STATUS, whose frequent palette-less NOTIFY clobbers a
  /// palette-full value. Null when the read fails; never throws. A legacy
  /// lamp that predates the section, a read before the auth handshake and an
  /// empty palette all read as an empty list.
  Future<List<LampColor>?> readManualPalette() async {
    try {
      final bytes = await _ble.readSection(_deviceId, 'wisppalette');
      return decodeManualPalette(bytes);
    } catch (_) {
      return null;
    }
  }

  /// Decode the raw `wisppalette` blob: interleaved R,G,B,W, one 4-byte
  /// group per color. A trailing partial group is dropped.
  static List<LampColor> decodeManualPalette(Uint8List bytes) {
    final usable = bytes.length - (bytes.length % 4);
    final out = <LampColor>[];
    for (var i = 0; i + 3 < usable; i += 4) {
      out.add(LampColor(
        r: bytes[i],
        g: bytes[i + 1],
        b: bytes[i + 2],
        w: bytes[i + 3],
      ));
    }
    return out;
  }

  /// Best-effort read of CHAR_WISP_CLAIMS. Returns the claimed mesh MACs and
  /// live paint colors, or null on any failure (legacy firmware, timeout,
  /// transient error). Null means "unknown, don't filter". Time-bounded so
  /// it can't stall the shared BLE flow.
  Future<WispClaimsParse?> readClaims() async {
    try {
      final bytes = await _ble
          .read(_deviceId, BleUuids.controlService, BleUuids.wispClaims)
          .timeout(const Duration(seconds: 4));
      return parseWispClaims(bytes);
    } catch (_) {
      return null;
    }
  }

  /// Source mode. Wisp persists in NVS and applies the
  /// appropriate transition (Off → lamps ease home; Manual → push stored
  /// palette into CurrentPalette; Osc → paint from the Straylight stream).
  Future<void> setSource(WispSourceMode mode) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setSource',
      'mode': wispSourceModeWire(mode),
    });
  }

  /// Straylight palette role the wisp follows in Osc mode. Persisted in
  /// wisp NVS.
  Future<void> setOscRole(OscRole role) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setOscRole',
      'role': oscRoleWire(role),
    });
  }

  /// Payload budget for a wispOp: the lamp's BLE stack takes at most 512 B
  /// per attribute write (`BLE_ATT_ATTR_MAX_LEN`), under the 576 B its
  /// MSG_CONTROL_OP relay carries. A larger write fails. The widest palette
  /// op, [kManualPaletteMaxColors] colours with W and the widest `seq`, fits.
  static const int kWispOpPayloadCap = 512;

  /// The most colours a Manual palette holds, as the wisp caps it: the most a
  /// Straylight palette carries.
  static const int kManualPaletteMaxColors = 16;

  /// Manual palette: its first [kManualPaletteMaxColors] colours. The wisp
  /// persists them in NVS and, if currently in Manual mode, pushes them into
  /// CurrentPalette so the lamps repaint without a mode flip. Each color is
  /// emitted as `[r,g,b,w]`, collapsed to `[r,g,b]` when w == 0.
  ///
  /// A [seq] (1..65535) rides the op as `seq`; a wisp that knows it echoes
  /// it as the status's `manualSeq` until the next save.
  ///
  /// Always plaintext, even when a password is set. Colors are low-value;
  /// the proximity threat model accepts unauthenticated palette pushes.
  Future<void> setManualPalette(List<LampColor> palette, {int? seq}) async {
    await _writeOpPlaintext(buildManualPaletteOp(palette, seq: seq));
  }

  /// Serialize the setManualPalette envelope for the first
  /// [kManualPaletteMaxColors] colours of [palette].
  static Map<String, dynamic> buildManualPaletteOp(
    List<LampColor> palette, {
    int? seq,
  }) =>
      {
        'char': 'wispOp',
        'op': 'setManualPalette',
        'colors': [
          for (final c in palette.take(kManualPaletteMaxColors))
            c.w != 0 ? [c.r, c.g, c.b, c.w] : [c.r, c.g, c.b],
        ],
        'seq': ?seq,
      };

  /// Content hash of [palette] as the wisp stores it, over the canonical
  /// wire bytes the wisp stamps as its palette id: r,g,b per color, plus w
  /// only when w != 0. Hashes the colors [buildManualPaletteOp] emits, its
  /// first [kManualPaletteMaxColors]. Returns 8 lowercase hex chars; the
  /// notifier confirms a palette write by matching this against the wisp's
  /// `paletteIdPrefix` on the status NOTIFY.
  static String paletteIdHash(List<LampColor> palette) =>
      manualPaletteId(_opColors(buildManualPaletteOp(palette)));

  /// The wisp's `manualPaletteId` of [colors] exactly as given: 8 lowercase
  /// hex chars.
  static String manualPaletteId(List<LampColor> colors) {
    final wire = <int>[];
    for (final c in colors) {
      wire.addAll([c.r, c.g, c.b]);
      if (c.w != 0) wire.add(c.w);
    }
    return fnv1a(Uint8List.fromList(wire)).toRadixString(16).padLeft(8, '0');
  }

  /// [colors] as the wisp status's `manualId` names them: `""` when empty,
  /// else [manualPaletteId].
  static String statusManualId(List<LampColor> colors) =>
      colors.isEmpty ? '' : manualPaletteId(colors);

  /// The `manualId` a wisp reports after storing the op [setManualPalette]
  /// sends for [palette] and [seq]: the colors that op carries.
  static String sentManualId(List<LampColor> palette, {int? seq}) =>
      statusManualId(_opColors(buildManualPaletteOp(palette, seq: seq)));

  static List<LampColor> _opColors(Map<String, dynamic> op) => [
        for (final c in op['colors'] as List)
          LampColor(
            r: (c as List)[0] as int,
            g: c[1] as int,
            b: c[2] as int,
            w: c.length > 3 ? c[3] as int : 0,
          ),
      ];

  /// Set the wisp password. Sealed under the CURRENT password when one is
  /// set; plaintext on a factory-fresh wisp (no shared secret yet).
  Future<void> setPassword(String newPassword) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setPassword',
      'password': newPassword,
    });
  }

  /// Off-mode color the wisp renders on its OWN 30-pixel ring when
  /// sourceMode == Off. Does NOT broadcast paint to the lamp grid.
  /// PaintDistributor stays held off in Off mode, so this color exists
  /// only on the wisp itself, "operating it like a lamp" per the
  /// product UX.
  Future<void> setOffColor(LampColor color) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setOffColor',
      'color': color.w == 0
          ? <int>[color.r, color.g, color.b]
          : <int>[color.r, color.g, color.b, color.w],
    });
  }

  /// Push new WiFi credentials to the wisp. The wisp persists them in
  /// NVS and immediately kicks WifiLink to reconnect and StageBeacon to
  /// refresh its BLE advert (so pre-mesh lamps follow the new SSID on
  /// their next scan). The wisp's own connection state surfaces back
  /// through `WispStatus.wifiConnected` on the next status notify.
  ///
  /// Accepted threat: the WiFi PSK leaks both here on the BLE write to
  /// `CHAR_WISP_OP` and downstream when the lamp unicasts the wispOp as
  /// plaintext MSG_CONTROL_OP to its display wisp, readable over the air
  /// (ESP-NOW range ~30 m LoS). Fleet-wide
  /// mesh auth would close it but is rejected; bounded by physical proximity.
  Future<void> setWifi(String ssid, String password) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setWifi',
      'ssid': ssid,
      'pw': password,
    });
  }

  /// Ask the wisp to re-attempt WiFi association using its stored NVS
  /// credentials, clearing a channel-mismatch rejection. No payload: the
  /// wisp already holds the SSID/password.
  Future<void> wifiReconnect() async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'wifiReconnect',
    });
  }

  /// Configure the wisp LED strip byte order and pixel count. Persisted in
  /// wisp NVS; the wisp re-inits the strip at the new format and length.
  Future<void> setLedStrip(String ledType, int pixelCount) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setLedStrip',
      'ledType': ledType,
      'pixelCount': pixelCount,
    });
  }

  /// Set the space-brightness factor (0..100) the wisp asserts on its
  /// claimed lamps. Persisted in wisp NVS; the wisp re-asserts it to the
  /// claimed lamps immediately and periodically.
  Future<void> setBrightness(int pct) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setBrightness',
      'brightness': pct,
    });
  }

  /// Commit a new drift interval and fade percentage. The wisp persists both
  /// in NVS and restarts the drift engine at the new rate.
  Future<void> setDrift(int intervalMs, int fadePct) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setDrift',
      'intervalMs': intervalMs,
      'fadePct': fadePct,
    });
  }

  /// Set the wisp's human-readable name. Persisted in wisp NVS.
  Future<void> setName(String name) async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'setName',
      'name': name,
    });
  }

  /// Ask the wisp to re-broadcast its status now (read-only; no state
  /// change). Always plaintext: it rides the wisp's plaintext exception
  /// list so a password-protected wisp still refreshes without the password.
  Future<void> pollStatus() async {
    await _writeOpPlaintext({
      'char': 'wispOp',
      'op': 'pollStatus',
    });
  }

  /// Ask the wisp to re-broadcast its wispConfig now (read-only). Plaintext
  /// for the same reason as [pollStatus]. A wisp that predates the op
  /// ignores it.
  Future<void> pollConfig() async {
    await _writeOpPlaintext({
      'char': 'wispOp',
      'op': 'pollConfig',
    });
  }

  /// Re-roll per-lamp color assignments. The wisp bumps its shuffle seed,
  /// re-paints the fleet with the new assignments, and broadcasts a fresh
  /// wispStatus so the app preview re-rolls in lock-step.
  Future<void> shuffle() async {
    await _writeOp({
      'char': 'wispOp',
      'op': 'shuffle',
    });
  }

  /// Write [payload] sealed under the cached password, or plaintext if none.
  Future<void> _writeOp(Map<String, dynamic> payload) async {
    final pw = _password;
    final Uint8List bytes;
    if (pw != null && pw.isNotEmpty) {
      bytes = await LampCrypto.encryptOp(
        op: payload,
        password: pw,
        saltUuid16: _wispOpSalt,
        charShortName: 'wispOp',
      );
    } else {
      bytes = Uint8List.fromList(utf8.encode(jsonEncode(payload)));
    }
    await _ble.write(
      _deviceId,
      BleUuids.controlService,
      BleUuids.wispOp,
      bytes,
      allowLongWrite: true,
    );
  }

  /// Write [payload] as bare plaintext JSON, regardless of password state.
  Future<void> _writeOpPlaintext(Map<String, dynamic> payload) async {
    await _ble.write(
      _deviceId,
      BleUuids.controlService,
      BleUuids.wispOp,
      Uint8List.fromList(utf8.encode(jsonEncode(payload))),
      allowLongWrite: true,
    );
  }
}
