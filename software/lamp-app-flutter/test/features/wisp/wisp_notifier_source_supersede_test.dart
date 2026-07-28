import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';

/// A superseded setSource must stop resending: an older mode's late resend
/// landing after a newer setSource is what flips the wisp back (Off then
/// quick Manual reverts to Off seconds later).
void main() {
  const lampId = 'supersede-lamp';

  ProviderContainer makeContainer(InMemoryBleClient ble) {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    return c;
  }

  int wispOpModeWrites(InMemoryBleClient ble, String mode) {
    return ble.writesTo(lampId, BleUuids.wispOp).where((bytes) {
      try {
        final m = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
        return m['op'] == 'setSource' && m['mode'] == mode;
      } catch (_) {
        return false;
      }
    }).length;
  }

  test('setSource(off) then quick setSource(manual): off stops resending',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(
          utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"aurora"}')),
    );
    final c = makeContainer(ble);
    // Keep the provider alive across the confirm-timeout wait; a bare read
    // lets it autodispose and rebuild to a null-value loading state.
    c.listen(wispNotifierProvider(lampId), (_, _) {});
    await c.read(wispNotifierProvider(lampId).future);

    final n = c.read(wispNotifierProvider(lampId).notifier);

    // Off fires first (arms its confirm + sends once), then Manual supersedes.
    final offFut = n.setSource(WispSourceMode.off);
    final manualFut = n.setSource(WispSourceMode.manual);

    // The wisp echoes Manual, confirming that op; Off's confirm never matches,
    // so its loop only stops via the supersede check on its next attempt.
    ble.simulateNotify(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(
          utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
    );

    await manualFut;
    await offFut;

    expect(wispOpModeWrites(ble, 'off'), 1,
        reason: 'superseded Off op sent once, no resends');
    expect(wispOpModeWrites(ble, 'manual'), greaterThanOrEqualTo(1));

    final after = c.read(wispNotifierProvider(lampId)).value!;
    expect(after.source, WispSourceMode.manual,
        reason: 'newer op wins; state stays Manual');
  }, timeout: const Timeout(Duration(seconds: 40)));

  test('setSource(manual) twice while unconfirmed: no revert, no throw',
      () async {
    final ble = InMemoryBleClient();
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(
          utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"off"}')),
    );
    final c = makeContainer(ble);
    c.listen(wispNotifierProvider(lampId), (_, _) {});
    await c.read(wispNotifierProvider(lampId).future);

    final n = c.read(wispNotifierProvider(lampId).notifier);

    // Second tap on the same pending mode is a no-op; without the top-of-method
    // guard it spawns a racing loop whose rollback clobbers state back to Off
    // and throws.
    final firstFut = n.setSource(WispSourceMode.manual);
    final secondFut = n.setSource(WispSourceMode.manual);

    // The second call returns immediately without touching the wisp.
    await expectLater(secondFut, completes);

    ble.simulateNotify(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(
          utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","source":"manual"}')),
    );

    await expectLater(firstFut, completes);

    final after = c.read(wispNotifierProvider(lampId)).value!;
    expect(after.source, WispSourceMode.manual,
        reason: 'no-op second tap must not revert to Off');
  }, timeout: const Timeout(Duration(seconds: 40)));
}
