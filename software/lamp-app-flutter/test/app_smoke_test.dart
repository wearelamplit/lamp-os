import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/app.dart';
import 'package:lamp_app/core/ble/ble_permissions.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _AlwaysGranted implements BlePermissions {
  @override
  Future<bool> request() async => true;
  @override
  Future<bool> isGranted() async => true;
  @override
  Future<bool> isPermanentlyDenied() async => false;
  @override
  Future<void> openSettings() async {}
}

void main() {
  testWidgets('app boots and shows onboarding when inventory empty',
      (tester) async {
    SharedPreferences.setMockInitialValues({});
    await tester.pumpWidget(
      ProviderScope(child: LampApp(permissions: _AlwaysGranted())),
    );
    await tester.pumpAndSettle();
    expect(find.text('No lamps in your care yet'), findsOneWidget);
  });
}
