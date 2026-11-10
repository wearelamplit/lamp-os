import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/presentation/firmware_copy.dart';

void main() {
  group('a failed push in dev mode', () {
    // The owner's ruled table, one line per reason.
    const table = {
      FirmwareFailure.noAnswer:
          "Juniper didn't answer. Bring your phone closer and try again.",
      FirmwareFailure.setupFailed:
          "Couldn't start the lesson. Check Juniper is still connected and "
          'try again.',
      FirmwareFailure.busy:
          'Juniper is already learning from another lamp. Give it a few '
          'minutes.',
      FirmwareFailure.untrustedAtOffer:
          "Juniper didn't trust that download, so it passed. Delete it and "
          'let the app fetch a fresh copy.',
      FirmwareFailure.untrusted:
          "Juniper didn't trust that download, so it passed. Delete it and "
          'let the app fetch a fresh copy.',
      FirmwareFailure.linkLost:
          'Lost Juniper partway through. Bring your phone closer and try '
          'again.',
      FirmwareFailure.missingPieces:
          'Juniper kept missing pieces. Try again from closer by.',
      FirmwareFailure.noResult:
          'Juniper went quiet before saying how it went. If the version '
          "below hasn't changed in a minute, try again.",
      FirmwareFailure.scrambled:
          'Something got scrambled on the way to Juniper. Try again.',
      FirmwareFailure.storeFailed:
          "Juniper couldn't store the new tricks. Try again. If it keeps "
          'happening, the lamp needs a USB flash.',
      FirmwareFailure.unknown:
          "Juniper couldn't learn the new tricks. Try again.",
      FirmwareFailure.cacheMissing:
          'That download has gone missing. It comes back next time the app '
          'starts online.',
      FirmwareFailure.cacheUnverified:
          "That download doesn't check out. Delete it and let the app fetch "
          'a fresh copy.',
      FirmwareFailure.wrongLamp:
          'That download is for a different kind of lamp.',
      FirmwareFailure.cancelled:
          'Lesson cancelled. Juniper stops waiting for the rest in about a '
          'minute.',
    };

    test('reads a friendly line for every reason', () {
      expect(table.keys, unorderedEquals(FirmwareFailure.values));
      for (final MapEntry(key: failure, value: line) in table.entries) {
        expect(FirmwareCopy.failure(failure, 'Juniper'), line,
            reason: failure.name);
      }
    });

    test('only an offer the lamp turned down counts as declined', () {
      expect(
        FirmwareFailure.values.where((f) => f.declined),
        unorderedEquals([
          FirmwareFailure.busy,
          FirmwareFailure.untrustedAtOffer,
        ]),
      );
    });
  });

  test('a line that opens with the fallback name is capitalized', () {
    expect(FirmwareCopy.learned('your lamp'), startsWith('Your lamp '));
    expect(FirmwareCopy.missed('your lamp'), startsWith('Your lamp '));
    expect(FirmwareCopy.teaching('your lamp'), 'Teaching your lamp new tricks 🎓');
  });
}
