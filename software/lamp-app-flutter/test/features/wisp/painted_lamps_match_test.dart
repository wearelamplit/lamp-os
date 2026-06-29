import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart'
    show resolvePaintedLamps, PaintedLampEntry;

void main() {
  LampNearbyPeer peer(String bd, String name) =>
      LampNearbyPeer(name: name, bdAddr: bd);

  test('null claimed -> empty (show-all handled by caller, not here)', () {
    expect(resolvePaintedLamps(claimed: null, peers: const []), isEmpty);
  });

  test('every claimed bdAddr appears; name from peers when present', () {
    final peers = [peer('FC:B4:67:F1:DD:A6', 'grady')];
    final out = resolvePaintedLamps(
      claimed: {'FC:B4:67:F1:DD:A6', 'AA:BB:CC:DD:EE:02'},
      peers: peers,
    );
    expect(out.length, 2);
    expect(out.firstWhere((e) => e.bdAddr == 'FC:B4:67:F1:DD:A6').name, 'grady');
    // unresolved claim still shows, labeled by a short bdAddr tail
    expect(out.firstWhere((e) => e.bdAddr == 'AA:BB:CC:DD:EE:02').name,
        contains('EE:02'));
  });
}
