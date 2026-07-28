/// Formats [d] as its two most-significant non-zero units (h/m/s/ms),
/// dropping a trailing zero unit. `2h 4m`, `1m 30s`, `450ms`.
String formatDuration(Duration d) {
  final ms = d.inMilliseconds;
  if (ms <= 0) return '0ms';
  final h = ms ~/ 3600000;
  final m = (ms % 3600000) ~/ 60000;
  final s = (ms % 60000) ~/ 1000;
  final msRem = ms % 1000;
  const suffixes = ['h', 'm', 's', 'ms'];
  final values = [h, m, s, msRem];
  final top = values.indexWhere((v) => v != 0);
  final parts = ['${values[top]}${suffixes[top]}'];
  if (top + 1 < values.length && values[top + 1] != 0) {
    parts.add('${values[top + 1]}${suffixes[top + 1]}');
  }
  return parts.join(' ');
}
