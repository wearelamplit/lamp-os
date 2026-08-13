import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/lamp_shell/presentation/info_screen.dart';

LsigFooter _footer() => LsigFooter(
      version: 0x010203,
      channel: 'standard-beta',
      signedRegionLen: 0,
      signature: Uint8List(0),
    );

Future<void> _pump(WidgetTester tester, FirmwareState state) =>
    tester.pumpWidget(MaterialApp(
      home: Scaffold(body: LearningTricksCard(state: state)),
    ));

void main() {
  testWidgets('streaming shows the tricks copy + a determinate bar',
      (tester) async {
    await _pump(
      tester,
      FirmwareStreaming(footer: _footer(), chunksSent: 1, totalChunks: 4),
    );
    expect(find.text('Learning some new tricks 🪄'), findsOneWidget);
    final bar = tester.widget<LinearProgressIndicator>(
        find.byType(LinearProgressIndicator));
    expect(bar.value, closeTo(0.25, 0.001));
  });

  testWidgets('verifying shows warming-up + an indeterminate bar',
      (tester) async {
    await _pump(tester, const FirmwareVerifying());
    expect(find.text('Warming up 🪄'), findsOneWidget);
    final bar = tester.widget<LinearProgressIndicator>(
        find.byType(LinearProgressIndicator));
    expect(bar.value, isNull);
  });
}
