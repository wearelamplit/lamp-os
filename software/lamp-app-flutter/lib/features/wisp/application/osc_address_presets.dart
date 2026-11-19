import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../domain/wisp_source_mode.dart';

const _presetsKey = 'wisp_osc_presets.v1';

/// Custom OSC addresses the owner saved, kept on this phone and shared by
/// every wisp, in the order saved. Only addresses the wisp's rule takes are
/// kept. Changes apply in the order made.
class OscAddressPresets extends AsyncNotifier<List<String>> {
  Future<void> _lastWrite = Future.value();

  @override
  Future<List<String>> build() async {
    final prefs = await SharedPreferences.getInstance();
    return [
      for (final a in prefs.getStringList(_presetsKey) ?? const <String>[])
        if (oscCustomAddressValid(a)) a,
    ];
  }

  Future<void> add(String address) => _change((cur) {
    if (!oscCustomAddressValid(address) || cur.contains(address)) return null;
    return [...cur, address];
  });

  Future<void> remove(String address) => _change((cur) {
    if (!cur.contains(address)) return null;
    return [
      for (final a in cur)
        if (a != address) a,
    ];
  });

  // Each change reads the list the one before it wrote, so two quick taps
  // cannot both start from the same list and lose one.
  Future<void> _change(List<String>? Function(List<String> cur) next) {
    final write = _lastWrite.then((_) async {
      final changed = next(await future);
      if (changed == null) return;
      final prefs = await SharedPreferences.getInstance();
      await prefs.setStringList(_presetsKey, changed);
      state = AsyncData(changed);
    });
    _lastWrite = write.catchError((Object _) {});
    return write;
  }
}

final oscAddressPresetsProvider =
    AsyncNotifierProvider<OscAddressPresets, List<String>>(
      OscAddressPresets.new,
    );
