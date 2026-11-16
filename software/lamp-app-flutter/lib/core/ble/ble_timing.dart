import 'package:flutter/foundation.dart';

enum BleTimingPhase { scanSeen, connect, discover, firstRead }

class BleTimingEvent {
  const BleTimingEvent(
    this.phase,
    this.deviceId,
    this.ms, {
    this.ok = true,
    this.attempt,
    this.attempts,
    this.totalMs,
    this.sinceLinkMs,
    this.section,
    this.scanSession,
  });

  final BleTimingPhase phase;
  final String deviceId;
  final int ms;
  final bool ok;

  /// 1-based connect attempt and the cap it ran under.
  final int? attempt;
  final int? attempts;

  /// connect ok: call entry to link up, backoff between attempts included.
  final int? totalMs;

  /// firstRead: link up to the end of the read.
  final int? sinceLinkMs;
  final String? section;

  /// scanSeen: the scan this sighting belongs to. Every scan (re)start opens a
  /// new one, counted from 1 per app run.
  final int? scanSession;

  @override
  String toString() {
    final f = <String>[
      '[ble_timing]',
      phase.name,
      'id=$deviceId',
      ok ? 'ok' : 'fail',
      if (attempt != null) 'attempt=$attempt/$attempts',
      if (section != null) 'section=$section',
      if (scanSession != null) 'scan_session=$scanSession',
      'ms=$ms',
      if (totalMs != null) 'total_ms=$totalMs',
      if (sinceLinkMs != null) 'since_link_ms=$sinceLinkMs',
    ];
    return f.join(' ');
  }
}

void _debugPrintEvent(BleTimingEvent e) => debugPrint(e.toString());

/// Records how long each phase of getting a lamp connected takes and hands
/// each as one [BleTimingEvent] to [sink]. Read the default sink with
/// `adb logcat -s flutter | grep ble_timing`.
///
/// Recording only reads a clock and calls the sink synchronously; a throwing
/// sink is swallowed so timing can never fail a connect.
class BleTimings {
  void Function(BleTimingEvent) sink = _debugPrintEvent;

  Duration Function() now = _monotonic;
  static final _clock = Stopwatch()..start();
  static Duration _monotonic() => _clock.elapsed;

  final _linkUp = <String, Duration>{};
  final _seenInScan = <String>{};
  Duration? _scanStart;
  int _scanSession = 0;

  @visibleForTesting
  int get scanSessionForTest => _scanSession;

  void _emit(BleTimingEvent e) {
    try {
      sink(e);
    } catch (_) {}
  }

  int _since(Duration t) => (now() - t).inMilliseconds;

  /// A scan (re)started. Sightings from here on belong to a new session.
  void scanStarted() {
    _scanSession++;
    _scanStart = now();
    _seenInScan.clear();
  }

  /// A lamp advertisement arrived. Only the first per lamp per scan session
  /// is recorded, as scan start to sighting.
  void sighting(String deviceId) {
    final start = _scanStart;
    if (start == null || !_seenInScan.add(deviceId)) return;
    _emit(
      BleTimingEvent(
        BleTimingPhase.scanSeen,
        deviceId,
        _since(start),
        scanSession: _scanSession,
      ),
    );
  }

  void connectFailed(
    String deviceId,
    Duration attemptStart,
    int attempt,
    int attempts,
  ) {
    _emit(
      BleTimingEvent(
        BleTimingPhase.connect,
        deviceId,
        _since(attemptStart),
        ok: false,
        attempt: attempt,
        attempts: attempts,
      ),
    );
  }

  /// The link is up. Arms the first-read measurement for [deviceId].
  void connectSucceeded(
    String deviceId,
    Duration callStart,
    Duration attemptStart,
    int attempt,
    int attempts,
  ) {
    _emit(
      BleTimingEvent(
        BleTimingPhase.connect,
        deviceId,
        _since(attemptStart),
        attempt: attempt,
        attempts: attempts,
        totalMs: _since(callStart),
      ),
    );
    _linkUp[deviceId] = now();
  }

  /// The link to [deviceId] went down; no first read is owed for it.
  void linkDown(String deviceId) => _linkUp.remove(deviceId);

  void discovered(String deviceId, Duration started) =>
      _emit(BleTimingEvent(BleTimingPhase.discover, deviceId, _since(started)));

  /// A section read started at [readStart] finished. Counts as the first read
  /// only when the link it ran on came up before it started.
  void sectionRead(
    String deviceId,
    String section,
    Duration readStart, {
    required bool ok,
  }) {
    final up = _linkUp[deviceId];
    if (up == null || up > readStart) return;
    _linkUp.remove(deviceId);
    _emit(
      BleTimingEvent(
        BleTimingPhase.firstRead,
        deviceId,
        _since(readStart),
        ok: ok,
        section: section,
        sinceLinkMs: _since(up),
      ),
    );
  }
}

/// The phone has one scan and one set of links, so one recorder is shared by
/// the scanner and every client.
final bleTimings = BleTimings();
