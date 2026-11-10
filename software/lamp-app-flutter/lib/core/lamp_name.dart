import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart' show StringCharacters;

/// Longest lamp name, in UTF-8 bytes: the firmware's `kMaxLampNameLen`
/// (`util/str.hpp`), which cuts anything longer on write.
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
/// edit that would push the text past the cap keeps as much of it as fits,
/// unless that leaves less than the text already had. Text still being
/// composed by an input method is left alone until it commits.
class Utf8ByteLimitFormatter extends TextInputFormatter {
  const Utf8ByteLimitFormatter(this.maxBytes);

  final int maxBytes;

  @override
  TextEditingValue formatEditUpdate(
    TextEditingValue oldValue,
    TextEditingValue newValue,
  ) {
    if (newValue.composing.isValid || _bytes(newValue.text) <= maxBytes) {
      return newValue;
    }
    final kept = StringBuffer();
    var used = 0;
    for (final c in newValue.text.characters) {
      final n = _bytes(c);
      if (used + n > maxBytes) break;
      kept.write(c);
      used += n;
    }
    final oldBytes = _bytes(oldValue.text);
    if (oldBytes <= maxBytes && used <= oldBytes) return oldValue;
    final text = kept.toString();
    return TextEditingValue(
      text: text,
      selection: TextSelection.collapsed(offset: text.length),
    );
  }

  static int _bytes(String s) => utf8.encode(s).length;
}
