import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/wisp_source_mode.dart';

const _presetsKey = 'wisp_osc_presets.v1';

/// Custom OSC addresses the owner saved, kept on this phone and shared by
/// every wisp, in the order saved. Only addresses the wisp's rule takes are
/// kept.
class OscAddressPresets extends AsyncNotifier<List<String>> {
  @override
  Future<List<String>> build() async {
    final prefs = await SharedPreferences.getInstance();
    return [
      for (final a in prefs.getStringList(_presetsKey) ?? const <String>[])
        if (oscCustomAddressValid(a)) a,
    ];
  }

  Future<void> add(String address) async {
    final cur = await future;
    if (!oscCustomAddressValid(address) || cur.contains(address)) return;
    await _store([...cur, address]);
  }

  Future<void> remove(String address) async {
    final cur = await future;
    if (!cur.contains(address)) return;
    await _store([
      for (final a in cur)
        if (a != address) a,
    ]);
  }

  Future<void> _store(List<String> next) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(_presetsKey, next);
    state = AsyncData(next);
  }
}

final oscAddressPresetsProvider =
    AsyncNotifierProvider<OscAddressPresets, List<String>>(
      OscAddressPresets.new,
    );
