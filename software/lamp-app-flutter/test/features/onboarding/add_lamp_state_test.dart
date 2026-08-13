import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/onboarding/domain/add_lamp_state.dart';

void main() {
  test('initial state is at scan step with empty fields', () {
    const s = AddLampState();
    expect(s.step, AddLampStep.scan);
    expect(s.deviceId, '');
    expect(s.name, '');
    expect(s.password, '');
    expect(s.status, AddLampStatus.idle);
  });

  test('copyWith advances step', () {
    const s = AddLampState();
    final next = s.copyWith(step: AddLampStep.name, deviceId: 'aa');
    expect(next.step, AddLampStep.name);
    expect(next.deviceId, 'aa');
    expect(next.name, '');
  });
}
