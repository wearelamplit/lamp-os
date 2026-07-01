import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';

void main() {
  // A nextChunk callback that serves [chunks] in order, then empty forever.
  Future<Uint8List> Function() server(List<Uint8List> chunks) {
    final q = List<Uint8List>.from(chunks);
    return () async => q.isEmpty ? Uint8List(0) : q.removeAt(0);
  }

  Uint8List bytes(int n) => Uint8List.fromList(List.filled(n, 0xAB));

  test('accumulates multi-chunk and terminates on the empty chunk', () async {
    final out = await readPagesUntilEmpty(
      'dev1',
      server([bytes(200), bytes(100), Uint8List(0)]),
    );
    expect(out.length, 300);
  });

  test('a short NON-final first chunk does not end the stream', () async {
    // The exact truncation bug this loop was written to fix: a sub-MTU chunk
    // was once treated as end-of-stream, silently dropping the rest.
    final out = await readPagesUntilEmpty(
      'dev1',
      server([bytes(50), bytes(200), Uint8List(0)]),
    );
    expect(out.length, 250,
        reason: 'must keep reading past a short non-final chunk');
  });

  test('empty first chunk yields zero bytes (auth-gated / unseeded section)',
      () async {
    final out = await readPagesUntilEmpty('dev1', server([Uint8List(0)]));
    expect(out, isEmpty);
  });

  test('throws BleReadTooLarge when the terminator never arrives', () async {
    // A wedged firmware cursor that never emits the empty chunk — the runaway
    // the cap exists to bound.
    Future<Uint8List> forever() async => bytes(1000);
    expect(
      () => readPagesUntilEmpty('dev1', forever, cap: 2048),
      throwsA(isA<BleReadTooLarge>()),
    );
  });
}
