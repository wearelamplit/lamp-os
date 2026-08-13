import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/nearby/presentation/mesh_lamps_screen.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';

void main() {
  group('otaPillFor', () {
    test('sender resolves to a "->" send pill', () {
      final pill = otaPillFor(
        const LampNearbyPeer(name: 'Flora', otaState: 1),
        sendingTo: 'Gramp',
      );
      expect(pill, isNotNull);
      expect(pill!.label, '→ Gramp');
      expect(pill.isSend, isTrue);
    });

    test('derived receiver resolves to a "<-" amber pill', () {
      // otaState 0: the receiver is HELLO-silent, edge derived from a sender.
      final pill = otaPillFor(
        const LampNearbyPeer(name: 'Gramp'),
        receivingFrom: 'Flora',
      );
      expect(pill, isNotNull);
      expect(pill!.label, '← Flora');
      expect(pill.isSend, isFalse);
    });

    test('peer not in any OTA has no pill', () {
      final pill = otaPillFor(const LampNearbyPeer(name: 'Idle'));
      expect(pill, isNull);
    });
  });

  group('fwLabel', () {
    test('no reported version renders a dash', () {
      expect(fwLabel(0, ''), 'fw —');
    });

    test('0.0.1 OTA-catch legacy lamp renders verbatim', () {
      expect(fwLabel(0x00000001, ''), 'fw 0.0.1');
    });

    test('version with channel appends the channel', () {
      expect(fwLabel(0x00010203, 'standard-beta'),
          'fw 1.2.3 · standard-beta');
    });
  });
}
