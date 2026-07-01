import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_svg/flutter_svg.dart';

import '../../domain/lamp_color.dart';
import 'critter_asset.dart';

/// Renders a critter SVG recolored from caller-supplied shade + base colors.
/// Pure stateless — no provider watching, no caching. Use for one-shot
/// renders (adopt-confirm) where a heavy ConsumerStatefulWidget is overkill.
///
/// The SVG substitution logic mirrors LampPreview: replaces the two
/// linearGradient blocks (ids ending in `Shade` and `Body`) with stops
/// derived from [shadeColors] and [baseColors].
// ponytail: no widget cache / template memoization — adopt-confirm renders once.
class RecoloredCritter extends StatelessWidget {
  const RecoloredCritter({
    super.key,
    required this.deviceId,
    required this.shadeColors,
    required this.baseColors,
    this.size = 120,
  });

  final String deviceId;
  final List<LampColor> shadeColors;
  final List<LampColor> baseColors;
  final double size;

  static final RegExp _shadePattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Shade"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );
  static final RegExp _bodyPattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Body"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );

  String _stopTag(double pct, String hex) =>
      '<stop offset="${pct.round()}%" '
      'style="stop-color:#$hex;stop-opacity:1"/>';

  String _buildStops(List<LampColor> colors) {
    if (colors.isEmpty) {
      return _stopTag(0, '000000') + _stopTag(100, '000000');
    }
    if (colors.length == 1) {
      final hex = colors.single.toRgbHex();
      return _stopTag(0, hex) + _stopTag(100, hex);
    }
    final n = colors.length;
    final buf = StringBuffer();
    for (var i = 0; i < n; i++) {
      buf.write(_stopTag(i / (n - 1) * 100, colors[i].toRgbHex()));
    }
    return buf.toString();
  }

  String _renderSvg(String template) {
    final shadeStops = _buildStops(shadeColors);
    final bodyStops = _buildStops(baseColors);
    String rewrite(String tag, String stops) =>
        '${tag.split('>').first}>$stops</linearGradient>';
    var out = template;
    out = out.replaceFirstMapped(_shadePattern, (m) => rewrite(m.group(0)!, shadeStops));
    out = out.replaceFirstMapped(_bodyPattern, (m) => rewrite(m.group(0)!, bodyStops));
    return out;
  }

  @override
  Widget build(BuildContext context) {
    final asset = critterAssetFor(critterIndex: null, deviceId: deviceId);
    return FutureBuilder<String>(
      // ponytail: new future each build; acceptable for a one-shot adopt render.
      future: rootBundle.loadString(asset),
      builder: (context, snap) {
        if (!snap.hasData) {
          return SizedBox(width: size, height: size);
        }
        return SvgPicture.string(
          _renderSvg(snap.data!),
          width: size,
          height: size,
        );
      },
    );
  }
}
