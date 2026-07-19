import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/domain/tuple_sampler.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_painted_lamps.dart'
    show paintColorFor;

void main() {
  const red = LampColor(r: 255, g: 0, b: 0, w: 0);
  const blue = LampColor(r: 0, g: 0, b: 255, w: 0);
  const green = LampColor(r: 0, g: 255, b: 0, w: 0);
  const white = LampColor(r: 255, g: 255, b: 255, w: 0);

  // parseWispClaims keys colors by raw mesh mac (the blob carries mac bytes
  // directly), and _macAt formats them as uppercase colon-hex.
  const lampId = '10:20:30:40:50:60';

  const palette = [red, blue];

  test('live color wins over prediction when lampId is in livePaint', () {
    final live = {
      lampId: (base: green, shade: white),
    };
    final result = paintColorFor(
      lampId: lampId,
      livePaint: live,
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNotNull);
    expect(result!.base, green);
    expect(result.shade, white);
    // Verify the live color differs from the prediction so the test is meaningful.
    final pred = predictTuple(mac: parseMacFromBleId(lampId)!, palette: palette);
    final predPair = (base: pred!.base, shade: pred.shade);
    expect(result, isNot(predPair));
  });

  test('falls back to predictTuple when lampId absent from livePaint', () {
    final result = paintColorFor(
      lampId: lampId,
      livePaint: const {},
      palette: palette,
      shuffleSeed: 0,
    );
    final mac = parseMacFromBleId(lampId)!;
    final pred = predictTuple(mac: mac, palette: palette)!;
    expect(result, isNotNull);
    expect(result!.base, pred.base);
    expect(result.shade, pred.shade);
  });

  test('returns null when palette empty and no live entry', () {
    final result = paintColorFor(
      lampId: lampId,
      livePaint: const {},
      palette: const [],
      shuffleSeed: 0,
    );
    expect(result, isNull);
  });

  test('key derivation: livePaint keyed by lampId string hits correctly', () {
    final live = {lampId: (base: green, shade: white)};
    final result = paintColorFor(
      lampId: lampId,
      livePaint: live,
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNotNull);
    expect(result!.base, green);
    expect(result.shade, white);
  });

  test('iOS-style UUID lampId (no mesh mac) returns null without live entry', () {
    const iosId = '12345678-1234-1234-1234-123456789ABC';
    final result = paintColorFor(
      lampId: iosId,
      livePaint: const {},
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNull);
  });
}
