import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_password_store.dart';

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  const lampId = 'test-lamp';
  const wispMac = 'AA:BB:CC:DD:EE:FF';
  const macB = '11:22:33:44:55:66';

  ProviderContainer makeContainer({InMemoryBleClient? ble}) {
    final client = ble ?? InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(client)],
    );
    addTearDown(c.dispose);
    return c;
  }

  Future<void> primeStatus(InMemoryBleClient ble, String json) async {
    await ble.connect(lampId);
    await ble.write(
      lampId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      Uint8List.fromList(utf8.encode(json)),
    );
  }

  group('WispOpSealing — wire framing', () {
    test('setZone sealed with password produces 0x02-prefixed wispOp', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac"}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      // Cache a password then write an op.
      await n.cachePassword(wispMac, 'hunter2');
      await n.setZone(3);

      final wire = ble.writesTo(lampId, BleUuids.wispOp).last;
      expect(wire[0], LampCrypto.magicCiphertext,
          reason: 'sealed op must start with 0x02');
      // 1 byte magic + 12 nonce + 16 tag + >= 1 ciphertext
      expect(wire.length, greaterThan(1 + 12 + 16));
    });

    test('setZone sealed op round-trips with the correct salt/info', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac"}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      await n.cachePassword(wispMac, 'hunter2');
      await n.setZone(5);

      final wire = ble.writesTo(lampId, BleUuids.wispOp).last;
      final plain = await LampCrypto.decryptOpForTesting(
        wire,
        password: 'hunter2',
        saltUuid16: uuidSaltLE16(BleUuids.wispOp),
        charShortName: 'wispOp',
      );
      final decoded = jsonDecode(utf8.decode(plain)) as Map<String, dynamic>;
      expect(decoded['op'], 'setZone');
      expect(decoded['zoneId'], 5);
    });

    test('setZone without password sends plaintext (no 0x02 prefix)', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac"}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      // No password cached — plaintext.
      await n.setZone(3);

      final wire = ble.writesTo(lampId, BleUuids.wispOp).last;
      expect(wire[0], isNot(LampCrypto.magicCiphertext));
    });

    test('setManualPalette always plaintext even when password is cached', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac"}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      await n.cachePassword(wispMac, 'hunter2');
      // Trigger a setManualPalette write via the notifier (empty palette is valid).
      await n.setManualPalette();

      final wire = ble.writesTo(lampId, BleUuids.wispOp).last;
      // setManualPalette is always plaintext regardless of cached password.
      // Plaintext wispOps are bare JSON (no 0x01 prefix on the wisp path).
      expect(wire[0], isNot(LampCrypto.magicCiphertext));
      final decoded = jsonDecode(utf8.decode(wire)) as Map<String, dynamic>;
      expect(decoded['op'], 'setManualPalette');
    });
  });

  group('WispPasswordStore persistence', () {
    test('write + reload under same MAC', () async {
      final store = WispPasswordStore();
      await store.save(wispMac, 'sekret');
      final loaded = await store.load(wispMac);
      expect(loaded, 'sekret');
    });

    test('different MACs are independent', () async {
      final store = WispPasswordStore();
      await store.save(wispMac, 'sekret');
      await store.save(macB, 'other');
      expect(await store.load(wispMac), 'sekret');
      expect(await store.load(macB), 'other');
    });

    test('clear removes password for that MAC only', () async {
      final store = WispPasswordStore();
      await store.save(wispMac, 'sekret');
      await store.save(macB, 'other');
      await store.clear(wispMac);
      expect(await store.load(wispMac), isNull);
      expect(await store.load(macB), 'other');
    });

    test('missing MAC returns null', () async {
      final store = WispPasswordStore();
      expect(await store.load('FF:FF:FF:FF:FF:FF'), isNull);
    });
  });

  group('WispNotifier setPassword flow', () {
    // The sealed op is confirmed by the wisp advancing opSeq in a fresh
    // status broadcast; feed that back through the notify stream.
    void confirmOpSeq(InMemoryBleClient ble, {required int opSeq, bool hasPassword = true}) {
      ble.simulateNotify(
        lampId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(utf8.encode(
            '{"wispMac":"$wispMac","hasPassword":$hasPassword,"opSeq":$opSeq}')),
      );
    }

    Uint8List sealedWrite(InMemoryBleClient ble) => ble
        .writesTo(lampId, BleUuids.wispOp)
        .firstWhere((w) => w[0] == LampCrypto.magicCiphertext);

    test('setPassword factory-fresh sends plaintext setPassword op', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac","hasPassword":false}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final ok = await n.setPassword('newpass');
      expect(ok, isTrue);

      final wire = ble.writesTo(lampId, BleUuids.wispOp).last;
      // Factory-fresh: plaintext (no prior password to seal under).
      expect(wire[0], isNot(LampCrypto.magicCiphertext));
      final decoded = jsonDecode(utf8.decode(wire)) as Map<String, dynamic>;
      expect(decoded['op'], 'setPassword');
      expect(decoded['password'], 'newpass');
    });

    test('setPassword with current password seals under it and persists on opSeq confirm', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac","hasPassword":true}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      // Hold the autoDispose provider alive across the confirm awaits.
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final future = n.setPassword('newpass', currentPassword: 'oldpass');
      await Future<void>.delayed(const Duration(milliseconds: 50));
      confirmOpSeq(ble, opSeq: 1);
      expect(await future, isTrue);

      final wire = sealedWrite(ble);
      final plain = await LampCrypto.decryptOpForTesting(
        wire,
        password: 'oldpass',
        saltUuid16: uuidSaltLE16(BleUuids.wispOp),
        charShortName: 'wispOp',
      );
      final decoded = jsonDecode(utf8.decode(plain)) as Map<String, dynamic>;
      expect(decoded['op'], 'setPassword');
      expect(decoded['password'], 'newpass');

      // The confirmed new password is now cached: a later op seals under it.
      await n.setZone(7);
      final next = ble.writesTo(lampId, BleUuids.wispOp).last;
      final nextPlain = await LampCrypto.decryptOpForTesting(
        next,
        password: 'newpass',
        saltUuid16: uuidSaltLE16(BleUuids.wispOp),
        charShortName: 'wispOp',
      );
      expect((jsonDecode(utf8.decode(nextPlain)) as Map)['op'], 'setZone');
    });

    test('setPassword with WRONG current password returns false and leaves the store untouched', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac","hasPassword":true}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      final n = c.read(wispNotifierProvider(lampId).notifier);

      // No opSeq confirmation arrives → the wisp never accepted the seal.
      final ok = await n.setPassword('newpass', currentPassword: 'wrongpass');
      expect(ok, isFalse);
      expect(await WispPasswordStore().load(wispMac), isNull);
    });

    test('clearPassword seals under current password and clears cache on opSeq confirm', () async {
      final ble = InMemoryBleClient();
      await primeStatus(ble, '{"wispMac":"$wispMac","hasPassword":true}');
      final c = makeContainer(ble: ble);
      await c.read(wispNotifierProvider(lampId).future);
      // Hold the autoDispose provider alive across the confirm awaits.
      c.listen(wispNotifierProvider(lampId), (_, _) {});
      final n = c.read(wispNotifierProvider(lampId).notifier);

      final future = n.clearPassword('oldpass');
      await Future<void>.delayed(const Duration(milliseconds: 50));
      confirmOpSeq(ble, opSeq: 1, hasPassword: false);
      expect(await future, isTrue);

      final wire = sealedWrite(ble);
      final plain = await LampCrypto.decryptOpForTesting(
        wire,
        password: 'oldpass',
        saltUuid16: uuidSaltLE16(BleUuids.wispOp),
        charShortName: 'wispOp',
      );
      final decoded = jsonDecode(utf8.decode(plain)) as Map<String, dynamic>;
      expect(decoded['op'], 'setPassword');
      expect(decoded['password'], '');

      // After a confirmed clear the cached password is gone: next op goes plaintext.
      await n.setZone(1);
      final nextWire = ble.writesTo(lampId, BleUuids.wispOp).last;
      expect(nextWire[0], isNot(LampCrypto.magicCiphertext));
    });
  });
}
