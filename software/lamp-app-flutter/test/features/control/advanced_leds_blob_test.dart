import 'dart:convert';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

// The ATT attribute value limit both the phone's BLE stack and the lamp's
// NimBLE enforce on one write.
const _attValueMax = 512;

typedef _Seg = (String name, int px, List<String> colors);

// The widest data pin a segment can carry: two digits.
const _widestPin = 33;

// A role section in the shape the firmware's emitRoleTopology sends it, every
// segment's data pin moved to [_widestPin].
Map<String, dynamic> _roleJson(List<_Seg> segs, String byteOrder) => {
  'px': segs.fold<int>(0, (sum, s) => sum + s.$2),
  'bpp': byteOrder == 'GRBW' ? 4 : 3,
  'byteOrder': byteOrder,
  'colors': segs.first.$3,
  'segments': [
    for (final s in segs)
      {'name': s.$1, 'px': s.$2, 'pin': _widestPin, 'colors': s.$3},
  ],
};

class _Variant {
  const _Variant(this.name, this.base, this.shade, {this.byteOrder = 'GRBW'});
  final String name;
  final List<_Seg> base;
  final List<_Seg> shade;
  final String byteOrder;

  BaseSection get baseSection =>
      BaseSection.fromJson({..._roleJson(base, byteOrder), 'knockout': []});
  ShadeSection get shadeSection =>
      ShadeSection.fromJson(_roleJson(shade, byteOrder));
}

String _hex(int i) =>
    '#${(i * 37 % 256).toRadixString(16).padLeft(2, '0')}a5c3ff';

// Each variant's Config::Defaults (software/lamp-os/src/lamps/*), lioness sent
// as GRB so the byte order is seen to pass through, and one lamp
// at every cap in config_types.hpp (kMaxSegments, kMaxSegmentNameLen,
// kMaxSurfaceColors) with three-digit pixel counts, which no variant exceeds.
final _variants = <_Variant>[
  const _Variant(
    'standard',
    [
      ('Base', 35, ['#30078300']),
    ],
    [
      ('Shade', 38, ['#5A170000']),
    ],
  ),
  const _Variant(
    'loaf',
    [
      ('Base', 40, ['#30078300']),
    ],
    [
      ('Shade', 12, ['#5A170000']),
    ],
  ),
  const _Variant(
    'staff',
    [
      ('Base', 35, ['#30078300']),
    ],
    [
      ('Shade', 38, ['#5A170000']),
    ],
  ),
  const _Variant(
    'snafu',
    [
      ('Stem', 24, ['#30078300', '#64149600']),
    ],
    [
      ('Small Dots', 16, ['#962d0000', '#097d0400', '#003c7800', '#78003200']),
      ('Medium Dots', 12, ['#78100000', '#a03c0000', '#966e0000']),
      ('Big Dots', 9, ['#9b000000', '#612d2c00']),
    ],
  ),
  const _Variant(
    'lioness',
    [
      ('Main', 32, ['#30078300', '#64149600']),
      ('Lions', 18, ['#30078300']),
    ],
    [
      ('Shade', 36, ['#5A170000']),
    ],
    byteOrder: 'GRB',
  ),
  _Variant(
    'every cap',
    [
      for (var k = 0; k < 4; k++)
        (
          'Base segment ${k}x'.padRight(16, 'x'),
          255,
          [for (var c = 0; c < 8; c++) _hex(k * 8 + c)],
        ),
    ],
    [
      for (var k = 0; k < 4; k++)
        (
          'Shade segment $k'.padRight(16, 'x'),
          255,
          [for (var c = 0; c < 8; c++) _hex(k * 8 + c + 1)],
        ),
    ],
  ),
];

void main() {
  group('Advanced LEDs settings blob', () {
    for (final v in _variants) {
      test('${v.name}: fits one write once encrypted', () async {
        final blob = ControlNotifier.advancedLedsBlob(
          v.baseSection,
          v.shadeSection,
        );
        final wire = await LampCrypto.encryptOp(
          op: {...blob, 'reboot': true},
          password: 'sixteen-chars-pw',
          saltUuid16: uuidSaltLE16(BleUuids.settingsBlob),
          charShortName: 'settingsBlob',
        );
        expect(wire.length, lessThanOrEqualTo(_attValueMax));
      });

      test('${v.name}: carries only the strip format the firmware reads', () {
        final blob = ControlNotifier.advancedLedsBlob(
          v.baseSection,
          v.shadeSection,
        );
        expect(blob.keys, unorderedEquals(['base', 'shade']));
        for (final (role, segs) in [('base', v.base), ('shade', v.shade)]) {
          expect(blob[role], {
            'byteOrder': v.byteOrder,
            'segments': [
              for (final s in segs) {'px': s.$2, 'pin': _widestPin},
            ],
          }, reason: role);
        }
      });
    }

    test('a segment on its built-in pin sends pin 0', () {
      final shade = ShadeSection.fromJson(const {
        'px': 38,
        'bpp': 4,
        'colors': ['#5A170000'],
        'segments': [
          {'name': 'Shade', 'px': 38, 'pin': 0, 'colors': <String>[]},
        ],
      });
      final base = BaseSection.fromJson(const {
        'px': 35,
        'bpp': 4,
        'colors': ['#30078300'],
        'knockout': [],
        'segments': [
          {'name': 'Base', 'px': 35, 'pin': 27, 'colors': <String>[]},
        ],
      });
      final blob = ControlNotifier.advancedLedsBlob(base, shade);
      expect(blob['shade']['segments'], [
        {'px': 38, 'pin': 0},
      ]);
      expect(blob['base']['segments'], [
        {'px': 35, 'pin': 27},
      ]);
    });

    test('a lamp that reports no pin is sent none', () {
      final shade = ShadeSection.fromJson(const {
        'px': 38,
        'bpp': 4,
        'colors': ['#5A170000'],
        'segments': [
          {'name': 'Shade', 'px': 38, 'colors': <String>[]},
        ],
      });
      final base = BaseSection.fromJson(const {
        'px': 35,
        'bpp': 4,
        'colors': ['#30078300'],
        'knockout': [],
        'segments': [
          {'name': 'Base', 'px': 35, 'colors': <String>[]},
        ],
      });
      final blob = ControlNotifier.advancedLedsBlob(base, shade);
      expect(blob['shade']['segments'], [
        {'px': 38},
      ]);
      expect(blob['base']['segments'], [
        {'px': 35},
      ]);
    });

    test('a role without segments sends its role-level format', () {
      final base = BaseSection.fromJson(const {
        'px': 41,
        'bpp': 3,
        'byteOrder': 'BGR',
        'colors': ['#30078300'],
        'knockout': [],
      });
      final shade = ShadeSection.fromJson(const {
        'px': 27,
        'bpp': 4,
        'colors': ['#5A170000'],
      });
      final blob = ControlNotifier.advancedLedsBlob(base, shade);
      expect(blob['base'], {'px': 41, 'bpp': 3, 'byteOrder': 'BGR'});
      expect(blob['shade'], {'px': 27, 'bpp': 4, 'byteOrder': 'GRBW'});
    });
  });
  test('applyAdvancedLedsAndReboot writes that blob to a snafu', () async {
    SharedPreferences.setMockInitialValues({});
    const devId = 'snafu-1';
    const pw = 'hunter22';
    final snafu = _variants.firstWhere((v) => v.name == 'snafu');
    String segsJson(List<_Seg> segs) => jsonEncode([
      for (final s in segs)
        {'name': s.$1, 'px': s.$2, 'pin': _widestPin, 'colors': s.$3},
    ]);
    final ble = InMemoryBleClient();
    await seedControlBle(
      ble,
      deviceId: devId,
      // GRB base against a GRBW shade, so a role mix-up shows.
      baseBpp: 3,
      baseSegmentsJson: segsJson(snafu.base),
      shadeSegmentsJson: segsJson(snafu.shade),
    );
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c
        .read(inventoryNotifierProvider.notifier)
        .add(
          const InventoryLamp(id: devId, name: 'snafu', controlPassword: pw),
        );
    final sub = c.listen(controlNotifierProvider(devId), (_, _) {});
    addTearDown(sub.close);
    final state = await c.read(controlNotifierProvider(devId).future);

    await c
        .read(controlNotifierProvider(devId).notifier)
        .applyAdvancedLedsAndReboot(base: state.base, shade: state.shade);

    final writes = ble.writesTo(devId, BleUuids.settingsBlob);
    expect(writes, hasLength(1));
    expect(writes.single.length, lessThanOrEqualTo(_attValueMax));
    final plain = await LampCrypto.decryptOpForTesting(
      writes.single,
      password: pw,
      saltUuid16: uuidSaltLE16(BleUuids.settingsBlob),
      charShortName: 'settingsBlob',
    );
    expect(jsonDecode(utf8.decode(plain)), {
      ...ControlNotifier.advancedLedsBlob(state.base, state.shade),
      'reboot': true,
    });
    expect(
      (jsonDecode(utf8.decode(plain)) as Map)['shade']['segments'],
      hasLength(3),
    );
    expect((jsonDecode(utf8.decode(plain)) as Map)['base']['byteOrder'], 'GRB');
    expect(
      (jsonDecode(utf8.decode(plain)) as Map)['shade']['segments'][2]['pin'],
      _widestPin,
    );
  });

  group('applyAdvancedLedsAndReboot pin check', () {
    // The in-memory lamp ignores the write, so it reloads its seeded pins.
    Future<(ProviderContainer, ControlNotifier)> pinnedSnafu() async {
      SharedPreferences.setMockInitialValues({});
      const devId = 'snafu-2';
      final snafu = _variants.firstWhere((v) => v.name == 'snafu');
      String segsJson(List<_Seg> segs) => jsonEncode([
        for (final s in segs)
          {'name': s.$1, 'px': s.$2, 'pin': _widestPin, 'colors': s.$3},
      ]);
      final ble = InMemoryBleClient();
      await seedControlBle(
        ble,
        deviceId: devId,
        baseSegmentsJson: segsJson(snafu.base),
        shadeSegmentsJson: segsJson(snafu.shade),
      );
      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      await c.read(inventoryNotifierProvider.future);
      await c.read(inventoryNotifierProvider.notifier).add(
            const InventoryLamp(id: devId, name: 'snafu', controlPassword: 'pw'),
          );
      final sub = c.listen(controlNotifierProvider(devId), (_, _) {});
      addTearDown(sub.close);
      addTearDown(c.dispose);
      await c.read(controlNotifierProvider(devId).future);
      return (c, c.read(controlNotifierProvider(devId).notifier));
    }

    test('names the role whose pin did not read back', () async {
      final (c, n) = await pinnedSnafu();
      n.setSegmentPin('shade', 1, 13);
      final state = c.read(controlNotifierProvider('snafu-2')).value!;
      final mismatches = await n.applyAdvancedLedsAndReboot(
        base: state.base,
        shade: state.shade,
      );
      expect(mismatches, contains('shade.pin'));
      expect(mismatches, isNot(contains('base.pin')));
    });

    test('reports nothing when every pin reads back', () async {
      final (c, n) = await pinnedSnafu();
      final state = c.read(controlNotifierProvider('snafu-2')).value!;
      final mismatches = await n.applyAdvancedLedsAndReboot(
        base: state.base,
        shade: state.shade,
      );
      expect(mismatches, isEmpty);
    });
  });
}
