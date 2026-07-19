import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_svg/flutter_svg.dart';

import '../../features/control/presentation/widgets/critter_asset.dart';

/// Small recolored critter SVG for list rows.
///
/// Used on surfaces (My Lamps picker, Social proximity list) that want
/// the lamp's actual critter rather than a generic glyph. Pass `shade`
/// + `base` Flutter colors and the widget handles the SVG load, gradient
/// recolor, and per-instance LRU.
///
/// Distinct from `LampPreview`, which watches `controlNotifierProvider`
/// per row. CritterIcon takes plain `Color` params, so the consumer
/// owns whatever reactivity feeds the colors (typically the My Lamps
/// `inventoryNotifierProvider` + `nearbyLampsNotifierProvider` watches
/// already at the screen level). No new Riverpod subscriptions per row.
class CritterIcon extends StatefulWidget {
  const CritterIcon({
    super.key,
    required this.deviceId,
    required this.shade,
    required this.base,
    this.size = 44,
  });

  final String deviceId;
  final Color shade;
  final Color base;
  final double size;

  @override
  State<CritterIcon> createState() => _CritterIconState();
}

class _CritterIconState extends State<CritterIcon> {
  // Asset string templates shared across all CritterIcon instances. The
  // load is one-time per asset path; subsequent mounts hit the map.
  static final Map<String, String> _templates = {};

  static final RegExp _shadePattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Shade"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );
  static final RegExp _bodyPattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Body"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );

  String? _localTemplate;
  String? _loadedFor;

  // 8-entry LRU of built SvgPicture widgets keyed by (shadeHex|baseHex).
  // Smaller than LampPreview's cache (16) because a list row doesn't
  // sweep through color samples; it lands on a single (shade, base)
  // pair and stays there. 8 covers the rare cross-row color overlap.
  static const int _cacheSize = 8;
  final Map<String, SvgPicture> _svgCache = {};
  final List<String> _svgCacheOrder = [];

  Future<void> _ensureLoaded(String assetPath) async {
    final cached = _templates[assetPath];
    if (cached != null) {
      if (_loadedFor != assetPath) {
        setState(() {
          _localTemplate = cached;
          _loadedFor = assetPath;
        });
      }
      return;
    }
    final s = await rootBundle.loadString(assetPath);
    _templates[assetPath] = s;
    if (!mounted) return;
    setState(() {
      _localTemplate = s;
      _loadedFor = assetPath;
    });
  }

  String _hexOf(Color c) {
    final v = c.toARGB32() & 0xFFFFFF;
    return v.toRadixString(16).padLeft(6, '0');
  }

  String _stopTag(double pct, String hex) =>
      '<stop offset="${pct.round()}%" '
      'style="stop-color:#$hex;stop-opacity:1"/>';

  String _renderSvg(String template, String shadeHex, String bodyHex) {
    final shadeStops = _stopTag(0, shadeHex) + _stopTag(100, shadeHex);
    final bodyStops = _stopTag(0, bodyHex) + _stopTag(100, bodyHex);

    String rewrite(String tag, String stops) {
      final openTag = '${tag.split('>').first}>';
      return '$openTag$stops</linearGradient>';
    }

    var out = template;
    out = out.replaceFirstMapped(
        _shadePattern, (m) => rewrite(m.group(0)!, shadeStops));
    out = out.replaceFirstMapped(
        _bodyPattern, (m) => rewrite(m.group(0)!, bodyStops));
    return out;
  }

  @override
  Widget build(BuildContext context) {
    final asset = critterAssetFor(widget.deviceId);

    if (_loadedFor != asset) {
      _ensureLoaded(asset);
    }

    final template = _localTemplate;
    if (template == null) {
      return SizedBox(width: widget.size, height: widget.size);
    }

    final shadeHex = _hexOf(widget.shade);
    final bodyHex = _hexOf(widget.base);
    final cacheKey = '$shadeHex|$bodyHex';

    SvgPicture? cached = _svgCache[cacheKey];
    if (cached == null) {
      final rendered = _renderSvg(template, shadeHex, bodyHex);
      cached = SvgPicture.string(
        rendered,
        width: widget.size,
        height: widget.size,
      );
      _svgCache[cacheKey] = cached;
      _svgCacheOrder.add(cacheKey);
      while (_svgCacheOrder.length > _cacheSize) {
        _svgCache.remove(_svgCacheOrder.removeAt(0));
      }
    }
    return SizedBox(
      width: widget.size,
      height: widget.size,
      child: cached,
    );
  }
}
