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

/// [name] cut to [kMaxLampNameBytes] at a character boundary. A field can
/// hand over text an input method is still composing, which its formatter
/// lets past the cap on some platforms, so a name is fitted again where it
/// leaves the field.
String fitLampName(String name) =>
    const Utf8ByteLimitFormatter(kMaxLampNameBytes)._prefix(name);

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
    final after = newValue.text;
    final (start, end) = _editedRange(oldValue, after);
    final head = after.substring(0, start);
    final tail = after.substring(end);
    var room = maxBytes - _bytes(head) - _bytes(tail);
    final String text;
    final int cursor;
    if (room < 0) {
      text = _prefix(after);
      cursor = text.length;
    } else {
      final fitted = StringBuffer();
      for (final c in after.substring(start, end).characters) {
        final n = _bytes(c);
        if (n > room) break;
        fitted.write(c);
        room -= n;
      }
      text = '$head$fitted$tail';
      cursor = head.length + fitted.length;
    }
    final composing = newValue.composing;
    return TextEditingValue(
      text: text,
      selection: TextSelection.collapsed(offset: cursor),
      composing: composing.isValid
          ? TextRange(
              start: min(composing.start, text.length),
              end: min(composing.end, text.length),
            )
          : TextRange.empty,
    );
  }

  /// Where [after] differs from [oldValue]: the old selection, replaced, when
  /// [after] keeps the text either side of it, else the span between the
  /// longest shared start and end. Never splits a surrogate pair.
  static (int, int) _editedRange(TextEditingValue oldValue, String after) {
    final before = oldValue.text;
    final selection = oldValue.selection;
    if (selection.isValid) {
      final head = before.substring(0, selection.start);
      final tail = before.substring(selection.end);
      if (after.length >= head.length + tail.length &&
          after.startsWith(head) &&
          after.endsWith(tail)) {
        return (head.length, after.length - tail.length);
      }
    }
    final shorter = min(before.length, after.length);
    var start = 0;
    while (start < shorter &&
        before.codeUnitAt(start) == after.codeUnitAt(start)) {
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
    return (start, end);
  }

  /// The longest whole-character prefix of [text] that fits.
  String _prefix(String text) {
    final kept = StringBuffer();
    var used = 0;
    for (final c in text.characters) {
      final n = _bytes(c);
      if (used + n > maxBytes) break;
      kept.write(c);
      used += n;
    }
    return kept.toString();
  }

  static bool _isLeadSurrogate(int unit) => unit >= 0xD800 && unit <= 0xDBFF;
  static bool _isTrailSurrogate(int unit) => unit >= 0xDC00 && unit <= 0xDFFF;

  static int _bytes(String s) => utf8.encode(s).length;
}
