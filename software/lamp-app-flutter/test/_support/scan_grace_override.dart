import 'package:lamp_app/features/nearby/application/scan_grace_provider.dart';

/// Test override that pins `scanGraceActiveProvider` to `false` so no
/// background Timer is scheduled. Widget tests dispose their container
/// in `addTearDown`, which runs AFTER the test framework's
/// `!timersPending` invariant check — so the production provider's
/// onDispose Timer-cancel comes too late. Use this in every test
/// container that hosts a widget tree which (transitively) reads the
/// scan-grace state.
final scanGraceTestOverride = scanGraceActiveProvider.overrideWith(
  _FixedScanGrace.new,
);

class _FixedScanGrace extends ScanGraceActive {
  @override
  bool build() => false;
}
