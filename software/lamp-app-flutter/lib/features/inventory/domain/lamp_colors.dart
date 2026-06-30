import 'package:flutter/material.dart';

import '../../control/domain/lamp_color.dart';
import '../../nearby/domain/nearby_lamp.dart';
import 'inventory_lamp.dart';

/// Single source of truth for a lamp tile's colors. Combines two sources:
///
/// - Live BLE adv (`near.baseRgb`/`near.shadeRgb`): authoritative for RGB,
///   regardless of which device drove the change. Carries no W byte.
/// - Inventory cache (`InventoryLamp.last{Shade,Base}Color`): RGBW, written
///   by `controlNotifier._updateSeen`; supplies W and covers the offline case.
///
/// Policy: adv present -> adv RGB + cached W; adv absent -> cached RGBW; both
/// absent -> null. A `shadeRgb` of 0 is treated as "missing" not "black"
/// (transitional v2 firmware ships 0 as a sentinel), so it falls through to
/// cache. Length-3 legacy cache entries parse as W=0.
class LampColors {
  const LampColors({this.shade, this.base});
  final Color? shade;
  final Color? base;
}

LampColors resolveLampColors({
  InventoryLamp? inv,
  NearbyLamp? near,
}) {
  int cachedW(List<int>? c) =>
      (c != null && c.length >= 4) ? c[3] : 0;

  Color? swatchFromList(List<int>? c) {
    if (c == null || c.length < 3) return null;
    final w = c.length >= 4 ? c[3] : 0;
    return LampColor(r: c[0], g: c[1], b: c[2], w: w).toSwatch();
  }

  // Adv carries authoritative RGB; cache supplies the W byte the
  // adv can't (no room in the 9-byte mfg payload). Blend through
  // LampColor.toSwatch so warm-heavy lamps don't look black after
  // a recent edit.
  Color swatchFromAdv(int rgb, int w) => LampColor(
        r: (rgb >> 16) & 0xFF,
        g: (rgb >> 8) & 0xFF,
        b: rgb & 0xFF,
        w: w,
      ).toSwatch();

  final Color? base = (near != null)
      ? swatchFromAdv(near.baseRgb, cachedW(inv?.lastBaseColor))
      : swatchFromList(inv?.lastBaseColor);

  Color? shade;
  if (near != null && near.shadeRgb != 0) {
    shade = swatchFromAdv(near.shadeRgb, cachedW(inv?.lastShadeColor));
  }
  shade ??= swatchFromList(inv?.lastShadeColor);

  return LampColors(shade: shade, base: base);
}
