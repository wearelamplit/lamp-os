import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_painted_lamps.dart'
    show resolvePaintedLamps;

void main() {
  LampNearbyPeer peer(String bd, String name) =>
      LampNearbyPeer(name: name, bdAddr: bd);

  test('null claimed -> empty (show-all handled by caller, not here)', () {
    expect(resolvePaintedLamps(claimed: null, peers: const []), isEmpty);
  });

  test('bdAddr name lookup is case-insensitive', () {
    final peers = [peer('FC:B4:67:F1:DD:A6', 'grady')];
    final out = resolvePaintedLamps(
      claimed: {'fc:b4:67:f1:dd:a6'},
      peers: peers,
    );
    expect(out.length, 1);
    expect(out.first.name, 'grady');
  });

  test('connected lamp names itself (never in its own peer list)', () {
    final out = resolvePaintedLamps(
      claimed: {'FC:B4:67:F1:DD:A6'},
      peers: const [],
      selfBdAddr: 'FC:B4:67:F1:DD:A6',
      selfName: 'betty',
    );
    expect(out.single.name, 'betty');
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
