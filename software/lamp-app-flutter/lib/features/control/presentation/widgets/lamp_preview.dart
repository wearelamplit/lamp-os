import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';

import '../../../inventory/application/inventory_notifier.dart';
import '../../application/control_notifier.dart';
import '../../application/control_state.dart';
import '../../domain/lamp_color.dart';
import 'critter_asset.dart';

/// A small recolored lamp graphic that reflects the current shade + base
/// gradient in real time. Picks the same critter SVG as ConnectingView for
/// this lamp (derived from `InventoryLamp.lampId`, falling back to `deviceId`).
///
/// Each viable critter SVG carries two linearGradient defs whose ids end in
/// `Shade` and `Body`. On each rebuild the `<linearGradient …>…</linearGradient>`
/// blocks in the cached template are replaced with new blocks whose
/// `<stop>` elements carry the live colors.
///
/// Watches its own state slice via `controlNotifierProvider(lampId).select`,
/// scoped to `(shade.colors, base.colors)`. Sibling state changes
/// (brightness, expressions, etc.) don't trigger LampPreview rebuilds.
class LampPreview extends ConsumerStatefulWidget {
  const LampPreview({
    super.key,
    required this.deviceId,
    this.size = 140,
  });

  final String deviceId;
  final double size;

  @override
  ConsumerState<LampPreview> createState() => _LampPreviewState();
}

/// Subset of ControlState that LampPreview cares about. Two `_PreviewSlice`
/// values are equal iff the rendered SVG would be identical. Drives the
/// `.select`'s equality check so unchanged surfaces don't rebuild.
class _PreviewSlice {
  const _PreviewSlice(this.shadeColors, this.baseColors);
  final List<LampColor> shadeColors;
  final List<LampColor> baseColors;

  @override
  bool operator ==(Object other) {
    if (other is! _PreviewSlice) return false;
    if (shadeColors.length != other.shadeColors.length) return false;
    for (var i = 0; i < shadeColors.length; i++) {
      if (shadeColors[i] != other.shadeColors[i]) return false;
    }
    if (baseColors.length != other.baseColors.length) return false;
    for (var i = 0; i < baseColors.length; i++) {
      if (baseColors[i] != other.baseColors[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode =>
      Object.hash(Object.hashAll(shadeColors), Object.hashAll(baseColors));
}

_PreviewSlice _previewSliceFrom(AsyncValue<ControlState> async) {
  final state = async.value;
  if (state == null) {
    return const _PreviewSlice([], []);
  }
  return _PreviewSlice(state.shade.colors, state.base.colors);
}

class _LampPreviewState extends ConsumerState<LampPreview> {
  // Cache one template per asset path so the same SVG isn't re-read each
  // time a different lamp's preview mounts.
  static final Map<String, String> _templates = {};

  // Compiled once per process; the patterns are constant across builds.
  static final RegExp _shadePattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Shade"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );
  static final RegExp _bodyPattern = RegExp(
    r'<linearGradient[^>]*id="[^"]*Body"[^>]*>.*?</linearGradient>',
    dotAll: true,
  );

  String? _localTemplate;
  String? _loadedFor; // the asset path the local template was loaded from

  // Single-entry memoization for `_renderSvg`. Each rebuild that hits the
  // same shade + base color set returns the cached SVG string instead of
  // running three regex passes again. The cache key is the joined hex of
  // the inputs; values are cheap to compute and short.
  String? _memoKey;
  String? _memoRendered;
  String? _memoFor; // the template the cached result was rendered against

  // Bounded LRU of built SvgPicture widgets keyed by their rendered-color
  // signature. Even with the string-memo above, every rebuild constructed a
  // fresh `SvgPicture.string(...)` and flutter_svg re-decodes the SVG XML on
  // each mount; caching the widget hands back the SAME instance across
  // rebuilds for a color set already built. 16 entries covers a smooth drag
  // (~8-12 unique color samples) without growing unbounded.
  static const int _svgCacheSize = 16;
  final Map<String, SvgPicture> _svgWidgetCache = {};
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

  /// Build replacement `<stop>` elements for [colors], spreading them evenly
  /// from 0 % to 100 % along the gradient axis. Uses the `style="stop-color:…"`
  /// form to match the convention in the critter SVGs. flutter_svg renders
  /// the style-attribute form reliably but ignores `stop-color="…"` written
  /// as a direct attribute, which would leave the gradient uncolored.
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
      final pct = i / (n - 1) * 100;
      final hex = colors[i].toRgbHex();
      buf.write(_stopTag(pct, hex));
    }
    return buf.toString();
  }

  /// Substitute the gradient blocks in [template] with the live colors.
  String _renderSvg(String template, List<LampColor> shadeColors,
      List<LampColor> baseColors) {
    final shadeStops = _buildStops(shadeColors);
    final bodyStops = _buildStops(baseColors);

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
    // Watch ONLY this lamp's critter identity to avoid a rebuild storm:
    // watching the whole inventory list rebuilds LampPreview on every
    // setShadeColor / setBaseColors call, since _updateSeen writes
    // lastShadeColor + lastBaseColor back into inventory on every slider tick.
    final critterIdentity =
        ref.watch(inventoryNotifierProvider.select((async) {
      final lampId = async.value
          ?.firstWhereOrNull((l) => l.id == widget.deviceId)
          ?.lampId;
      return (lampId != null && lampId.isNotEmpty) ? lampId : widget.deviceId;
    }));
    // Targeted watch on the control state, scoped to only the slice that
    // affects the rendered SVG. Sibling state changes (brightness,
    // expressions, home, etc.) and the inventory writeback storm during a
    // slider drag don't rebuild this widget.
    final slice = ref.watch(
      controlNotifierProvider(widget.deviceId).select(_previewSliceFrom),
    );
    final asset = critterAssetFor(critterIdentity);

    // Kick off (or refresh) the load if this is a new asset path. Done here
    // rather than in initState because the chosen asset can change if the
    // inventory entry's lampId appears asynchronously.
    if (_loadedFor != asset) {
      _ensureLoaded(asset);
    }

    final template = _localTemplate;
    if (template == null) {
      return SizedBox(width: widget.size, height: widget.size);
    }
    final cacheKey =
        '${slice.shadeColors.map((c) => c.toHex()).join(",")}|${slice.baseColors.map((c) => c.toHex()).join(",")}';
    // Reuse an already-built SvgPicture widget for this cacheKey. Without it,
    // every shade/base hex change rebuilds SvgPicture with a fresh ValueKey,
    // forcing flutter_svg to re-decode the SVG from scratch (measurable jank
    // during continuous color drags). Keep up to _svgCacheSize recently-built
    // widgets keyed by the rendered-color signature; same-signature rebuilds
    // return the cached widget.
    SvgPicture? cached = _svgWidgetCache[cacheKey];
    if (cached == null) {
      String rendered;
      if (_memoFor == template &&
          _memoKey == cacheKey &&
          _memoRendered != null) {
        rendered = _memoRendered!;
      } else {
        rendered = _renderSvg(template, slice.shadeColors, slice.baseColors);
        _memoFor = template;
        _memoKey = cacheKey;
        _memoRendered = rendered;
      }
      cached = SvgPicture.string(
        rendered,
        // No explicit ValueKey: a key here would force Flutter to tear
        // down the element on every cache miss (a sustained drag visits
        // more unique color samples than the 16-entry LRU holds),
        // causing a one-frame mount gap and visible flicker.
        // flutter_svg's own internal picture cache still keys off the
        // String source content so unchanged renders return the same
        // decoded picture.
        width: widget.size,
        height: widget.size,
      );
      _svgWidgetCache[cacheKey] = cached;
      _svgCacheOrder.add(cacheKey);
      while (_svgCacheOrder.length > _svgCacheSize) {
        _svgWidgetCache.remove(_svgCacheOrder.removeAt(0));
      }
    }
    return SizedBox(
      width: widget.size,
      height: widget.size,
      child: cached,
    );
  }
}
