import 'package:riverpod_annotation/riverpod_annotation.dart';
import 'package:shared_preferences/shared_preferences.dart';

part 'active_lamp_notifier.g.dart';

const _activeKey = 'active_lamp.v1';

@Riverpod(keepAlive: true, name: 'activeLampNotifierProvider')
class ActiveLampNotifier extends _$ActiveLampNotifier {
  @override
  Future<String?> build() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_activeKey);
  }

  Future<void> set(String id) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_activeKey, id);
    state = AsyncData(id);
  }

  Future<void> clear() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_activeKey);
    state = const AsyncData(null);
  }
}
