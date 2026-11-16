import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';

const _lampId = 'reconnect-lamp';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// Notify delivery as flutter_blue_plus gives it against an unbonded lamp: a
/// new link starts with notifications off until the app subscribes again,
/// and once it has, every listener on the characteristic receives, a
/// listener left over from the old link included.
class _LinkScopedBle extends InMemoryBleClient {
  int _link = 0;
  int? _notifyingLink;

  /// Live listeners on the wisp status notify.
  int statusListeners = 0;

  @override
  Future<void> disconnect(String deviceId) {
    _link++;
    return super.disconnect(deviceId);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) {
    final src = super.subscribe(d, s, c);
    _notifyingLink = _link;
    StreamSubscription<Uint8List>? sub;
    late final StreamController<Uint8List> out;
    out = StreamController<Uint8List>(
      onListen: () {
        statusListeners++;
        sub = src.where((_) => _notifyingLink == _link).listen(out.add);
      },
      onCancel: () {
        statusListeners--;
        return sub?.cancel();
      },
    );
    return out.stream;
  }
}

/// What the wisp notifier hears after the lamp link drops and comes back
/// while the lamp stays open in the app.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  void notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
      _lampId, BleUuids.controlService, BleUuids.wispStatus, _b(json));

  test('a status pushed after a reconnect reaches the notifier, and a save '
      'made then confirms', () async {
    const saved = [LampColor(r: 1, g: 2, b: 3, w: 0)];
    const edit = [
      LampColor(r: 1, g: 2, b: 3, w: 0),
      LampColor(r: 70, g: 0, b: 200, w: 5),
    ];
    final ble = _LinkScopedBle();
    ble.seedSection(_lampId, 'wisppalette',
        Uint8List.fromList([for (final c in saved) ...[c.r, c.g, c.b, c.w]]));
    await ble.connect(_lampId);
    final manual = '{"wispMac":"$_mac","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(saved)}"}';
    await ble.write(
        _lampId, BleUuids.controlService, BleUuids.wispStatus, _b(manual));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_lampId), (_, _) {});
    await c.read(wispNotifierProvider(_lampId).future);
    final n = c.read(wispNotifierProvider(_lampId).notifier);
    expect(c.read(wispNotifierProvider(_lampId)).value!.source,
        WispSourceMode.manual);

    await ble.disconnect(_lampId);
    await pumpEventQueue();
    await ble.connect(_lampId);
    await pumpEventQueue();

    notify(ble, '{"wispMac":"$_mac","source":"osc"}');
    await pumpEventQueue();
    expect(c.read(wispNotifierProvider(_lampId)).value!.source,
        WispSourceMode.osc);
    expect(ble.statusListeners, 1,
        reason: 'the old link\'s listener would hear every notify twice');

    notify(ble, manual);
    await pumpEventQueue();
    n.setManualPaletteDraft(edit);
    Object? err;
    final save = n.setManualPalette().catchError((Object e) => err = e);
    await pumpEventQueue();
    notify(ble, '{"wispMac":"$_mac","source":"manual",'
        '"paletteIdPrefix":"${WispRepository.paletteIdHash(edit)}"}');
    await save;
    expect(err, isNull);
    expect(n.manualPaletteWriteFailed, isFalse);
    expect(n.savedManualPalette, edit);
  }, timeout: const Timeout(Duration(seconds: 40)));
}
