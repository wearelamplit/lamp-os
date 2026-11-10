import 'package:flutter_svg/flutter_svg.dart';
import 'package:flutter_test/flutter_test.dart';

/// The axis every critter asset's Shade gradient declares: top to bottom.
const kAssetShadeAxis = 'x1="0%" y1="0%" x2="0%" y2="100%"';

/// The SVG source of the one [SvgPicture] on screen.
String renderedSvg(WidgetTester tester) {
  final pic = tester.widget<SvgPicture>(find.byType(SvgPicture));
  // ignore: invalid_use_of_protected_member
  return (pic.bytesLoader as SvgStringLoader).provideSvg(null);
}

RegExpMatch _gradient(String svg, String idSuffix) => RegExp(
      '(<linearGradient[^>]*id="[^"]*$idSuffix"[^>]*>)(.*?)</linearGradient>',
      dotAll: true,
    ).firstMatch(svg)!;

/// The open tag of the gradient whose id ends in [idSuffix].
String gradientOpenTag(String svg, String idSuffix) =>
    _gradient(svg, idSuffix).group(1)!;

/// That gradient's stops as `offset:RRGGBB`, in document order.
List<String> gradientStops(String svg, String idSuffix) => [
      for (final m in RegExp(r'offset="([^"]+)"[^>]*stop-color:#([0-9A-Fa-f]{6})')
          .allMatches(_gradient(svg, idSuffix).group(2)!))
        '${m.group(1)}:${m.group(2)!.toUpperCase()}',
    ];
