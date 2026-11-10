import 'dart:convert';
import 'dart:math';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart' show StringCharacters;

/// Longest lamp name, in UTF-8 bytes: the firmware's `kMaxLampNameLen`
/// (software/lamp-os/src/util/str.hpp), which cuts anything longer on write.
const kMaxLampNameBytes = 32;

/// A lamp's name composes its setup WiFi SSID, its BLE advert name and its
/// mDNS host, so whitespace in one leaves the owner hunting for a network
/// that does not match the name the app shows. The firmware strips it on
/// write and keeps [kMaxLampNameBytes]; these keep both out of the field.
final lampNameFormatters = <TextInputFormatter>[
  FilteringTextInputFormatter.deny(RegExp(r'\s')),
  const Utf8ByteLimitFormatter(kMaxLampNameBytes),
];

/// Caps a field at [maxBytes] UTF-8 bytes, never splitting a character. An
/// edit that would pass the cap keeps the text around it and as much of what
/// it typed or pasted as fits. An unfinished composing region is capped when
/// the platform's default for [LengthLimitingTextInputFormatter] caps it.
class Utf8ByteLimitFormatter extends TextInputFormatter {
  const Utf8ByteLimitFormatter(this.maxBytes);

  final int maxBytes;

  @override
  TextEditingValue formatEditUpdate(
    TextEditingValue oldValue,
    TextEditingValue newValue,
  ) {
    if (_bytes(newValue.text) <= maxBytes) return newValue;
    if (newValue.composing.isValid &&
        LengthLimitingTextInputFormatter.getDefaultMaxLengthEnforcement() ==
            MaxLengthEnforcement.truncateAfterCompositionEnds) {
      return newValue;
    }
    final before = oldValue.text;
    final after = newValue.text;
    final shorter = min(before.length, after.length);
    var start = 0;
    while (start < shorter && before.codeUnitAt(start) == after.codeUnitAt(start)) {
      start++;
    }
    var kept = 0;
    while (kept < shorter - start &&
        before.codeUnitAt(before.length - 1 - kept) ==
            after.codeUnitAt(after.length - 1 - kept)) {
      kept++;
    }
    if (start > 0 && _isLeadSurrogate(after.codeUnitAt(start - 1))) start--;
    var end = after.length - kept;
    if (end < after.length && _isTrailSurrogate(after.codeUnitAt(end))) end++;
    final head = after.substring(0, start);
    final tail = after.substring(end);
    var room = maxBytes - _bytes(head) - _bytes(tail);
    if (room < 0) return _prefix(after);
    final fitted = StringBuffer();
    for (final c in after.substring(start, end).characters) {
      final n = _bytes(c);
      if (n > room) break;
      fitted.write(c);
      room -= n;
    }
    final cursor = head.length + fitted.length;
    return TextEditingValue(
      text: '$head$fitted$tail',
      selection: TextSelection.collapsed(offset: cursor),
    );
  }

  /// The longest whole-character prefix of [text] that fits, for an edit
  /// whose untouched text is already past the cap.
  TextEditingValue _prefix(String text) {
    final kept = StringBuffer();
    var used = 0;
    for (final c in text.characters) {
      final n = _bytes(c);
      if (used + n > maxBytes) break;
      kept.write(c);
      used += n;
    }
    final out = kept.toString();
    return TextEditingValue(
      text: out,
      selection: TextSelection.collapsed(offset: out.length),
    );
  }

  static bool _isLeadSurrogate(int unit) => unit >= 0xD800 && unit <= 0xDBFF;
  static bool _isTrailSurrogate(int unit) => unit >= 0xDC00 && unit <= 0xDFFF;

  static int _bytes(String s) => utf8.encode(s).length;
}
