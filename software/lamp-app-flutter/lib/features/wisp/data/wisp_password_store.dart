import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

/// Persists per-wisp control passwords keyed by wisp MAC address.
/// Separate from inventory_lamp.controlPassword: the wisp isn't an
/// inventory entry and its password is keyed by wispMac, not a lamp BLE id.
class WispPasswordStore {
  // ponytail: flat JSON map in one prefs key; split to per-key if MAC count grows large.
  static const _prefsKey = 'wisp_passwords.v1';

  Future<Map<String, String>> _readMap() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_prefsKey);
    if (raw == null || raw.isEmpty) return {};
    try {
      return Map<String, String>.from(
        (jsonDecode(raw) as Map<String, dynamic>).cast<String, String>(),
      );
    } catch (_) {
      return {};
    }
  }

  Future<void> _writeMap(Map<String, String> map) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsKey, jsonEncode(map));
  }

  /// Persist [password] for [wispMac]. Overwrites any prior value.
  Future<void> save(String wispMac, String password) async {
    final map = await _readMap();
    map[wispMac] = password;
    await _writeMap(map);
  }

  /// Load the stored password for [wispMac], or null if none.
  Future<String?> load(String wispMac) async {
    final map = await _readMap();
    return map[wispMac];
  }

  /// Remove the password for [wispMac]. Other MACs are unaffected.
  Future<void> clear(String wispMac) async {
    final map = await _readMap();
    map.remove(wispMac);
    await _writeMap(map);
  }
}
