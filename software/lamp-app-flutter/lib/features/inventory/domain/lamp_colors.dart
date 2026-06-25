import 'package:flutter/material.dart';

import '../../control/domain/lamp_color.dart';
import '../../nearby/domain/nearby_lamp.dart';
import 'inventory_lamp.dart';

/// Single source of truth for "what colors should this lamp's tile
/// display". Combines two sources:
///
/// - **Live BLE adv** (`near.baseRgb` / `near.shadeRgb`) — always
///   authoritative for the **RGB** triplet. Updates regardless of
///   which device drove the change (this phone, another phone, a
///   remote, an autonomous expression). The adv carries no W byte
///   today (the 9-byte mfg payload is `[magic, baseRGB, shadeRGB,
///   version]` — see `ble_scanner.dart:94-97`).
/// - **Inventory cache** (`InventoryLamp.last{Shade,Base}Color`) —
///   populated by `controlNotifier._updateSeen` on every successful
///   connect-and-read and every live slider tick. Stored as RGBW
///   (4 ints). Supplies the W byte the adv can't carry; also
///   covers the offline case when there's no recent BLE sighting.
///
/// Resolution policy:
///   - Adv present → use adv RGB + cached W (if any), blended for screen.
///   - Adv absent → use full cached RGBW.
///   - Both absent → null (consumer renders neutral grey fallback).
///
/// One exception: a `NearbyLamp.shadeRgb` of `0` is treated as
/// "missing data" rather than "actually black" — transitional v2
/// firmware in the field (post-mesh-flag, pre-shade-restore) ships
/// `0` as a sentinel, and we'd rather fall through to cache than
/// paint a real black where we have a known-good cached value.
///
/// Length-3 legacy cache entries (written before this layer learned
/// to store W) are tolerated — they parse as W=0, which renders the
/// same as the previous behaviour.
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
