import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/domain/sections.dart';

// The ATT attribute value limit both the phone's BLE stack and the lamp's
// NimBLE enforce on one write.
const _attValueMax = 512;

typedef _Seg = (String name, int px, List<String> colors);

// A role section in the shape the firmware's emitRoleTopology sends it.
Map<String, dynamic> _roleJson(List<_Seg> segs, String byteOrder) => {
  'px': segs.fold<int>(0, (sum, s) => sum + s.$2),
  'bpp': byteOrder == 'GRBW' ? 4 : 3,
  'byteOrder': byteOrder,
  'colors': segs.first.$3,
  'segments': [
    for (final s in segs) {'name': s.$1, 'px': s.$2, 'colors': s.$3},
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

// Each variant's Config::Defaults (software/lamp-os/src/lamps/*), and one lamp
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
              for (final s in segs) {'px': s.$2},
            ],
          }, reason: role);
        }
      });
    }

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
}
