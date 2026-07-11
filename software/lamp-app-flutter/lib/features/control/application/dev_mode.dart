import 'dart:convert';

import 'package:cryptography/cryptography.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';
import 'package:shared_preferences/shared_preferences.dart';

part 'dev_mode.g.dart';

const _devModeKey = 'dev_mode.v1';

// SHA-256 of the dev-mode unlock password. Only the hash lives in the binary.
const _kDevModePasswordHashHex =
    'f7bc3d20eccd3fe6522c1c59e8752e7d85a33d66a560506dc0c83858bf4c2156';

Future<bool> devModePasswordMatches(String input) async {
  final hash = await Sha256().hash(utf8.encode(input));
  final hex = hash.bytes
      .map((b) => b.toRadixString(16).padLeft(2, '0'))
      .join();
  return hex == _kDevModePasswordHashHex;
}

@Riverpod(keepAlive: true, name: 'devModeProvider')
class DevMode extends _$DevMode {
  @override
  Future<bool> build() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool(_devModeKey) ?? false;
  }

  Future<void> enable() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_devModeKey, true);
    state = const AsyncData(true);
  }

  Future<void> disable() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_devModeKey, false);
    state = const AsyncData(false);
  }
}

final devModeOnProvider =
    Provider<bool>((ref) => ref.watch(devModeProvider).value ?? false);
