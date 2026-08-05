import 'package:shared_preferences/shared_preferences.dart';

/// Content-addressed cache of expression-catalog JSON, keyed by the lamp's
/// `catalogHash`. A given catalog is fetched over BLE once, ever: shared
/// across reconnects and across every lamp that reports the same hash. The
/// stored value is the raw `exprcat` JSON string, so a cache hit rebuilds
/// the exact same [ExpressionCatalog] a fresh read would.
class CatalogStore {
  const CatalogStore();

  // ponytail: one prefs key per hash; catalogs are ~9.5KB and effectively
  // ~2 distinct ones, so no eviction. Add LRU trim if the key count grows.
  static const _prefix = 'exprcat.v1.';

  /// Raw catalog JSON stored under [hash], or null on a miss.
  Future<String?> load(String hash) async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString('$_prefix$hash');
  }

  /// Persist raw catalog [json] under [hash]. Overwrites any prior value.
  Future<void> save(String hash, String json) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('$_prefix$hash', json);
  }
}
