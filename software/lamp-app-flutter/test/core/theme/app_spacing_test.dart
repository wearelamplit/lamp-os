import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_spacing.dart';

void main() {
  test('spacing + radius tokens', () {
    expect(AppSpace.md, 12);
    expect(AppSpace.lg, 16);
    expect(AppRadius.card, 12);
  });
}
