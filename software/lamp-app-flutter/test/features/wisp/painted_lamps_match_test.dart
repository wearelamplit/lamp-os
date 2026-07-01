import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_painted_lamps.dart'
    show resolvePaintedLamps, previewPaletteFor;

void main() {
  LampNearbyPeer peer(String bd, String name) =>
      LampNearbyPeer(name: name, bdAddr: bd);

  group('previewPaletteFor (only Manual paints a predictable grid palette)', () {
    const manual = [
      LampColor(r: 255, g: 0, b: 0, w: 0),
      LampColor(r: 0, g: 0, b: 255, w: 0),
    ];
    test('manual -> the manual palette', () {
      expect(previewPaletteFor(WispSourceMode.manual, manual), manual);
    });
    test('off -> empty (offColor is on the wisp ring, not the grid)', () {
      expect(previewPaletteFor(WispSourceMode.off, manual), isEmpty);
    });
    test('aurora -> empty (no app-side aurora palette)', () {
      expect(previewPaletteFor(WispSourceMode.aurora, manual), isEmpty);
    });
    test('null source -> empty', () {
      expect(previewPaletteFor(null, manual), isEmpty);
    });
  });

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
