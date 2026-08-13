import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import '../../_support/in_memory_ble_client.dart';

void main() {
  test('readSectionVia writes the section name to pageCtrl and returns empty on an empty-terminated read', () async {
    // Multi-chunk accumulation is covered by read_pages_until_empty_test.dart
    final ble = InMemoryBleClient();
    await ble.connect('lamp-1');
    // Seed pageData with empty bytes — the lamp's end-of-snapshot signal.
    await ble.write(
      'lamp-1',
      BleUuids.controlService,
      BleUuids.pageData,
      Uint8List(0),
    );

    final bytes = await readSectionVia(ble, 'lamp-1', 'lamp');

    expect(bytes, isEmpty);
    expect(utf8.decode(ble.writesTo('lamp-1', BleUuids.pageCtrl).last), 'lamp');
  });
}
