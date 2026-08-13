import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/routing/routes.dart';

void main() {
  group('AppRoutes', () {
    test('static literals', () {
      expect(AppRoutes.onboarding, '/onboarding');
      expect(AppRoutes.addLamp, '/onboarding/add');
      expect(AppRoutes.lampPicker, '/lamp-picker');
    });

    test('parameterized builders', () {
      expect(AppRoutes.lamp('abc'), '/lamp/abc');
      expect(AppRoutes.control('abc'), '/lamp/abc/control');
      expect(AppRoutes.expressions('abc'), '/lamp/abc/expressions');
      expect(AppRoutes.expressionEditor('abc', 'breathing', 3),
          '/lamp/abc/expressions/breathing/3');
      // Knockout moved under /setup/ (was /control/) when knockout entry
      // moved from the Control tab to the Setup tab in the row-list refactor.
      expect(AppRoutes.knockout('abc'), '/lamp/abc/setup/knockout');
      expect(AppRoutes.setup('abc'), '/lamp/abc/setup');
      expect(AppRoutes.info('abc'), '/lamp/abc/info');
      expect(AppRoutes.homeWifi('abc'), '/lamp/abc/setup/wifi');
      expect(AppRoutes.homeMode('abc'), '/lamp/abc/setup/home-mode');
      expect(AppRoutes.advancedLeds('abc'), '/lamp/abc/setup/advanced-leds');
    });
  });
}
