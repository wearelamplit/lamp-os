import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:flutter_test/flutter_test.dart';

import 'in_memory_ble_client.dart';

const wispA = 'AA:BB:CC:DD:EE:FF';
const wispB = '11:22:33:44:55:66';

/// The lamp's merge when it holds a HELLO from [mac] and no status: no
/// `source`, no settings, no `manualId`.
String helloOnlyStatus(String mac) =>
    '{"wispMac":"$mac","wispVersion":66048,"helloFlags":0}';

/// A full Manual status without `manualId`: the app takes its palette as
/// unknown.
String unstatedStatus(String mac, List<LampColor> p) => jsonEncode({
  'wispMac': mac,
  'configSplit': true,
  'source': 'manual',
  if (p.isNotEmpty) 'paletteIdPrefix': WispRepository.manualPaletteId(p),
});

/// A full Manual status from a wisp stating [p] as its palette (`manualId`),
/// echoing [seq] when given.
String statingStatus(String mac, List<LampColor> p, {int? seq}) => jsonEncode({
  'wispMac': mac,
  'configSplit': true,
  'source': 'manual',
  'manualId': WispRepository.statusManualId(p),
  'manualSeq': ?seq,
  if (p.isNotEmpty) 'paletteIdPrefix': WispRepository.manualPaletteId(p),
});

/// The status [mac] sends once it stores the last palette op written to
/// [lampId]: that op's colours as its palette, echoing that op's `seq`.
String echoOfLastPaletteSave(
  InMemoryBleClient ble,
  String lampId, {
  String mac = wispA,
}) {
  final op = ble
      .writesTo(lampId, BleUuids.wispOp)
      .map((w) => jsonDecode(utf8.decode(w)) as Map<String, dynamic>)
      .lastWhere((op) => op['op'] == 'setManualPalette');
  final colors = [
    for (final c in op['colors'] as List)
      LampColor(
        r: (c as List)[0] as int,
        g: c[1] as int,
        b: c[2] as int,
        w: c.length > 3 ? c[3] as int : 0,
      ),
  ];
  return statingStatus(mac, colors, seq: op['seq'] as int?);
}

Uint8List _utf8(String s) => Uint8List.fromList(utf8.encode(s));
Uint8List _bytes(List<LampColor> p) => Uint8List.fromList([
  for (final c in p) ...[c.r, c.g, c.b, c.w],
]);

/// A lamp whose palette, status and claims reads and palette and source
/// writes can be held open, and that counts palette reads.
class PaletteBle extends InMemoryBleClient {
  PaletteBle(this.lampId);
  final String lampId;

  int paletteReads = 0;
  Completer<void>? holdPalette;
  Completer<void>? holdPaletteWrite;
  Completer<void>? holdSourceWrite;
  Completer<void>? holdClaims;
  Completer<void>? holdStatusRead;

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    await super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
    final op = utf8.decode(v, allowMalformed: true);
    if (op.contains('setManualPalette')) await holdPaletteWrite?.future;
    if (op.contains('setSource')) await holdSourceWrite?.future;
  }

  @override
  Future<Uint8List> read(String d, String s, String c) async {
    if (c == BleUuids.wispClaims) await holdClaims?.future;
    return super.read(d, s, c);
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wisppalette') {
      paletteReads++;
      await holdPalette?.future;
    }
    if (name == 'wispstatus') await holdStatusRead?.future;
    return super.readSection(deviceId, name);
  }

  void notifyStatus(String json) => simulateNotify(
    lampId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    _utf8(json),
  );

  void seedPalette(List<LampColor> p) =>
      seedSection(lampId, 'wisppalette', _bytes(p));

  void seedStatus(String json) =>
      seedSection(lampId, 'wispstatus', _utf8(json));

  /// The palette ops written to the lamp so far.
  Iterable<Uint8List> get paletteWrites =>
      writesTo(lampId, BleUuids.wispOp).where(
        (w) =>
            utf8.decode(w, allowMalformed: true).contains('setManualPalette'),
      );
}

/// Opens the lamp on [initial] and builds its wisp notifier. [onEmit] runs on
/// every notification the provider sends its listeners.
Future<(ProviderContainer, WispNotifier)> openWisp(
  PaletteBle ble,
  String initial, {
  void Function()? onEmit,
}) async {
  await ble.connect(ble.lampId);
  ble.seedStatus(initial);
  final c = ProviderContainer(
    overrides: [bleClientProvider.overrideWithValue(ble)],
  );
  addTearDown(c.dispose);
  c.listen(wispNotifierProvider(ble.lampId), (_, _) => onEmit?.call());
  await c.read(wispNotifierProvider(ble.lampId).future);
  await pumpEventQueue();
  return (c, c.read(wispNotifierProvider(ble.lampId).notifier));
}
