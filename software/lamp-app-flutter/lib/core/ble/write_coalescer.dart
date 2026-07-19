import 'dart:async';
import 'dart:typed_data';

/// Throttles a stream of [schedule] calls so the BLE link sees at most one
/// write per [debounce] window plus a trailing write with the final value.
/// Used to keep slider drags from saturating the GATT queue while still
/// letting the lamp track the slider in real time.
///
/// Behavior:
///   1. The first `schedule` after an idle window fires immediately.
///   2. Subsequent calls within the window stash the payload as `_pending`.
///   3. When the window elapses the pending payload is drained.
///   4. Calls after the trailing drain start a fresh window at #1.
///
/// Writes are fired via `unawaited(onWrite(...))`. Pacing is driven by the
/// timer, not by the previous write's ACK. That means the lamp keeps
/// seeing a steady ~`1000ms/debounce` writes during a drag even when the
/// BLE link is under WIDE conn parameters or a WRITE-with-response commit
/// is queued in front. The trade-off is that fbp's per-device FIFO can
/// grow briefly under sustained slow-link conditions; the debounce value
/// is the floor that keeps that bounded.
///
/// Errors from [onWrite] are dropped: this is a fire-and-forget channel,
/// a failed mid-stream slider write should not crash the UI.
class WriteCoalescer {
  WriteCoalescer({required this.onWrite, required this.debounce});

  final Future<void> Function(Uint8List) onWrite;

  /// Minimum time between writes. Also bounds the latency between the last
  /// `schedule` and the trailing flush.
  final Duration debounce;

  Uint8List? _pending;
  Timer? _windowTimer;
  bool _windowOpen = false;
  bool _disposed = false;

  void schedule(Uint8List payload) {
    if (_disposed) return;
    if (!_windowOpen) {
      _windowOpen = true;
      _pending = null;
      unawaited(onWrite(payload));
      _windowTimer = Timer(debounce, _onWindowElapsed);
      return;
    }
    _pending = payload;
  }

  void _onWindowElapsed() {
    if (_disposed) return;
    final payload = _pending;
    _pending = null;
    if (payload != null) {
      unawaited(onWrite(payload));
      _windowTimer = Timer(debounce, _onWindowElapsed);
    } else {
      _windowOpen = false;
    }
  }

  /// Cancels any pending deferred flush without disabling the coalescer.
  /// Use before writing a superseding command so no stale payload lands
  /// after the command.
  void cancel() {
    _windowTimer?.cancel();
    _windowTimer = null;
    _pending = null;
    _windowOpen = false;
  }

  void dispose() {
    _disposed = true;
    _windowTimer?.cancel();
    _windowTimer = null;
    _pending = null;
    _windowOpen = false;
  }
}

/// Same semantics as [WriteCoalescer], but keyed: each [K] has its own
/// throttle window and pending payload so writes for different keys can
/// proceed concurrently while writes for the same key still coalesce to
/// the latest. Use when a single characteristic carries multiple
/// independent state streams (e.g. CHAR_EDIT_SESSION encodes a per-surface
/// open/close flag; collapsing across surfaces would lose the signal for
/// the other surface).
class KeyedWriteCoalescer<K> {
  KeyedWriteCoalescer({required this.onWrite, required this.debounce});

  final Future<void> Function(K key, Uint8List payload) onWrite;
  final Duration debounce;

  final Map<K, Uint8List?> _pending = {};
  final Map<K, Timer> _windowTimers = {};
  final Set<K> _windowOpen = {};
  bool _disposed = false;

  void schedule(K key, Uint8List payload) {
    if (_disposed) return;
    if (!_windowOpen.contains(key)) {
      _windowOpen.add(key);
      _pending[key] = null;
      unawaited(onWrite(key, payload));
      _windowTimers[key] = Timer(debounce, () => _onWindowElapsed(key));
      return;
    }
    _pending[key] = payload;
  }

  void _onWindowElapsed(K key) {
    if (_disposed) return;
    final payload = _pending[key];
    _pending[key] = null;
    if (payload != null) {
      unawaited(onWrite(key, payload));
      _windowTimers[key] = Timer(debounce, () => _onWindowElapsed(key));
    } else {
      _windowOpen.remove(key);
      _windowTimers.remove(key);
      _pending.remove(key);
    }
  }

  void dispose() {
    _disposed = true;
    for (final t in _windowTimers.values) {
      t.cancel();
    }
    _windowTimers.clear();
    _pending.clear();
    _windowOpen.clear();
  }
}
