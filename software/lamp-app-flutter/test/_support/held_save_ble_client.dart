import 'dart:async';
import 'dart:typed_data';

import 'package:lamp_app/core/ble/uuids.dart';

import 'in_memory_ble_client.dart';

/// A lamp whose settings_blob writes wait on [hold] while it is set, so a
/// test can look at a screen mid-save. Completing [hold] with an error makes
/// the write fail with it.
class HeldSaveBleClient extends InMemoryBleClient {
  Completer<void>? hold;

  /// settings_blob writes asked for, held or not.
  int saves = 0;

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    if (c == BleUuids.settingsBlob) {
      saves++;
      await hold?.future;
    }
    return super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
  }
}
