import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/presentation/widgets/critter_asset.dart';

void main() {
  test('critterIndex 1..16 maps to sixteen distinct critter SVGs', () {
    final picks = {
      for (var i = 1; i <= 16; i++)
        critterAssetFor(critterIndex: i, deviceId: 'x'),
    };
    expect(picks.length, 16);
  });

  test('null critterIndex falls back to a deterministic deviceId hash', () {
    expect(
      critterAssetFor(critterIndex: null, deviceId: 'lamp-A'),
      critterAssetFor(critterIndex: null, deviceId: 'lamp-A'),
    );
  });
}
