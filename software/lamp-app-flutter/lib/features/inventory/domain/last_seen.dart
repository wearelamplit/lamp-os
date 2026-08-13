/// Returns a short human-readable "last seen" label, e.g. "Just now",
/// "5m ago", "2h ago", "3d ago", "2 weeks ago", "5 months ago",
/// "over a year ago". Pure. Caller passes [now] so tests are
/// deterministic. Future timestamps (clock skew) clamp to "Just now".
String formatLastSeen(int epochMs, DateTime now) {
  final delta =
      now.difference(DateTime.fromMillisecondsSinceEpoch(epochMs));
  if (delta.inSeconds < 60) return 'Just now';
  if (delta.inMinutes < 60) return '${delta.inMinutes}m ago';
  if (delta.inHours < 24) return '${delta.inHours}h ago';
  if (delta.inDays < 14) return '${delta.inDays}d ago';
  if (delta.inDays < 56) return '${(delta.inDays / 7).floor()} weeks ago';
  final months = (delta.inDays / 30).floor();
  if (months >= 12) return 'over a year ago';
  return '$months months ago';
}
