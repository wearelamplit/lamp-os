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

  // bdAddr FC:B4:67:F1:DD:A6 → meshMac FC:B4:67:F1:DD:A4 (BLE - 2).
  // parseWispClaims keys colors by bdAddr (the blob contains bdAddr bytes
  // written by bdAddrFromMeshMac in wisp_cache.cpp:319).
  const bdAddr = 'FC:B4:67:F1:DD:A6';
  // Key format: uppercase colon-hex of bdAddr bytes, as _macAt emits.
  const bdAddrKey = 'FC:B4:67:F1:DD:A6';

  const palette = [red, blue];

  test('live color wins over prediction when bdAddr is in livePaint', () {
    final live = {
      bdAddrKey: (base: green, shade: white),
    };
    final result = paintColorFor(
      bdAddr: bdAddr,
      livePaint: live,
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNotNull);
    expect(result!.base, green);
    expect(result.shade, white);
    // Verify the live color differs from the prediction so the test is meaningful.
    final pred = predictTuple(mac: meshMacFromBdAddr(bdAddr)!, palette: palette);
    final predPair = (base: pred!.base, shade: pred.shade);
    expect(result, isNot(predPair));
  });

  test('falls back to predictTuple when bdAddr absent from livePaint', () {
    final result = paintColorFor(
      bdAddr: bdAddr,
      livePaint: const {},
      palette: palette,
      shuffleSeed: 0,
    );
    final mac = meshMacFromBdAddr(bdAddr)!;
    final pred = predictTuple(mac: mac, palette: palette)!;
    expect(result, isNotNull);
    expect(result!.base, pred.base);
    expect(result.shade, pred.shade);
  });

  test('returns null when palette empty and no live entry', () {
    final result = paintColorFor(
      bdAddr: bdAddr,
      livePaint: const {},
      palette: const [],
      shuffleSeed: 0,
    );
    expect(result, isNull);
  });

  test('key derivation: livePaint keyed by bdAddr string hits correctly', () {
    // livePaint is keyed by uppercase colon-hex bdAddr, matching parseWispClaims output.
    final live = {bdAddrKey: (base: green, shade: white)};
    final result = paintColorFor(
      bdAddr: bdAddr,
      livePaint: live,
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNotNull);
    expect(result!.base, green);
    expect(result.shade, white);
  });

  test('iOS-style UUID bdAddr (no mesh MAC) returns null without live entry', () {
    const iosId = '12345678-1234-1234-1234-123456789ABC';
    final result = paintColorFor(
      bdAddr: iosId,
      livePaint: const {},
      palette: palette,
      shuffleSeed: 0,
    );
    expect(result, isNull);
  });
}
