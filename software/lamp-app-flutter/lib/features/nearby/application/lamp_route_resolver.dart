import '../../../core/routing/routes.dart';
import '../../inventory/domain/inventory_lamp.dart';
import '../domain/nearby_lamp.dart';

/// Pick the right route for opening [lampId] based on what the BLE
/// scanner currently sees. A lamp whose adv reports
/// `isMesh: false` (legacy v1 firmware or any build pre-mesh-protocol)
/// can't complete the full GATT control handshake, so it goes to the
/// dedicated [AppRoutes.btOnly] info pane instead of [AppRoutes.control]
/// where the user would just be stuck on the ConnectingView.
///
/// Out-of-range lamps fall back to [InventoryLamp.lastKnownIsMesh] when
/// available — a legacy BT-only lamp that's currently silent should
/// still route to BtOnlyLampScreen instead of stranding the user on
/// ConnectingView. When neither nearby NOR inventory has a cached
/// `isMesh` (factory-fresh inventory entry, pre-fix inventory written
/// before `lastKnownIsMesh` existed), default to control — that's the
/// pre-fix behavior and the "optimistic" path that any newer lamp
/// satisfies once it comes back into range.
///
/// Centralizing this so every entry point — MyLamps tile,
/// AppBar picker sheet, post-adoption done step — uses the same logic.
String routeForLamp(
  String lampId,
  List<NearbyLamp> nearby, {
  List<InventoryLamp>? inventory,
}) {
  for (final n in nearby) {
    if (n.id == lampId) {
      return n.isMesh ? AppRoutes.control(lampId) : AppRoutes.btOnly(lampId);
    }
  }
  if (inventory != null) {
    for (final inv in inventory) {
      if (inv.id == lampId && inv.lastKnownIsMesh == false) {
        return AppRoutes.btOnly(lampId);
      }
    }
  }
  // Not in range AND no cached isMesh hint says BT-only: fall through
  // to control + ConnectingView (the lamp will land here when it comes
  // back into range, and the user sees "Connecting…" briefly rather
  // than a separate offline screen).
  return AppRoutes.control(lampId);
}
