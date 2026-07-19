import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/presentation/widgets/critter_asset.dart';

void main() {
  test('critterAssetFor derives a deterministic path from identity', () {
    expect(critterAssetFor('lamp-A'), critterAssetFor('lamp-A'));
    expect(
      critterAssetFor('AA:BB:CC:DD:EE:01'),
      'assets/critters/critter-${critterIndexFor('AA:BB:CC:DD:EE:01')}.svg',
    );
  });

  test('every critter index resolves to a distinct asset path', () {
    final picks = {
      for (var i = 1; i <= critterCount; i++) 'assets/critters/critter-$i.svg',
    };
    expect(picks.length, critterCount);
  });

  test('critterIndexFor is stable for a given identity', () {
    expect(critterIndexFor('AA:BB:CC:DD:EE:01'),
        critterIndexFor('AA:BB:CC:DD:EE:01'));
  });

  test('critterIndexFor is case-insensitive', () {
    expect(critterIndexFor('aa:bb:cc:dd:ee:01'),
        critterIndexFor('AA:BB:CC:DD:EE:01'));
  });

  test('critterIndexFor always lands in 1..16', () {
    for (final identity in ['', 'x', 'AA:BB:CC:DD:EE:01', 'a' * 200]) {
      expect(critterIndexFor(identity), inInclusiveRange(1, 16));
    }
  });

  test('critterIndexFor pins known identities to known indices', () {
    expect(critterIndexFor('AA:BB:CC:DD:EE:01'), 2);
    expect(critterIndexFor('11:22:33:44:55:66'), 13);
  });
}
