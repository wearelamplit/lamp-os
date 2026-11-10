import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/lamp_name.dart';

/// Runs the name field's formatters over a typed value the way a TextField
/// would, so the rule is asserted rather than assumed from the widget tree.
String _typed(String text, {String old = ''}) {
  final before = TextEditingValue(
    text: old,
    selection: TextSelection.collapsed(offset: old.length),
  );
  var value = TextEditingValue(
    text: text,
    selection: TextSelection.collapsed(offset: text.length),
  );
  for (final f in lampNameFormatters) {
    value = f.formatEditUpdate(before, value);
  }
  return value.text;
}

void main() {
  test('a typed space never reaches the name', () {
    expect(_typed('gramp '), 'gramp');
    expect(_typed(' gramp'), 'gramp');
    expect(_typed('big jim'), 'bigjim');
  });

  test('a clean name is untouched', () {
    expect(_typed('gramp'), 'gramp');
    expect(_typed('lamp2'), 'lamp2');
  });

  test('tabs and newlines are whitespace too', () {
    expect(_typed('big\tjim'), 'bigjim');
    expect(_typed('big\njim'), 'bigjim');
  });

  group("the name is capped at the firmware's byte limit", () {
    int bytes(String s) => utf8.encode(s).length;

    test('in UTF-8 bytes, so a multi-byte character counts in full', () {
      // 30 bytes of ASCII plus a 4-byte emoji is 32 UTF-16 units, under a
      // cap that counted characters or units, but 34 bytes.
      final ascii = 'a' * 30;
      expect(_typed('$ascii🎓'), ascii);
      expect(_typed('${'a' * 28}🎓'), '${'a' * 28}🎓');
      expect(_typed('é' * 17), 'é' * 16);
    });

    test('never splits a character', () {
      // A family emoji is one character of 18 bytes.
      const family = '👨‍👩‍👧';
      final out = _typed('${'b' * 20}$family');
      expect(out, 'b' * 20);
      expect(bytes(out), lessThanOrEqualTo(kMaxLampNameBytes));
    });

    test('a paste keeps as much as fits', () {
      final long = 'lampy' * 10;
      final out = _typed(long);
      expect(bytes(out), kMaxLampNameBytes);
      expect(long.startsWith(out), isTrue);
    });

    test('typing past the cap changes nothing', () {
      final full = 'c' * kMaxLampNameBytes;
      expect(_typed('${full}d', old: full), full);
      expect(_typed('${'c' * 16}d${'c' * 16}', old: full), full);
      final nearly = 'c' * 30;
      expect(_typed('$nearly🎓', old: nearly), nearly);
    });

    test('text an input method is still composing waits until it commits',
        () {
      final long = 'か' * 11;
      final composing = TextEditingValue(
        text: long,
        selection: TextSelection.collapsed(offset: long.length),
        composing: TextRange(start: 0, end: long.length),
      );
      const cap = Utf8ByteLimitFormatter(kMaxLampNameBytes);
      expect(cap.formatEditUpdate(TextEditingValue.empty, composing), composing);
      expect(
        cap
            .formatEditUpdate(
              TextEditingValue.empty,
              composing.copyWith(composing: TextRange.empty),
            )
            .text,
        'か' * 10,
      );
    });

    test("matches the firmware's kMaxLampNameLen", () {
      final hpp = File('../lamp-os/src/util/str.hpp').readAsStringSync();
      final limit = RegExp(r'kMaxLampNameLen = (\d+);').firstMatch(hpp);
      expect(int.parse(limit!.group(1)!), kMaxLampNameBytes);
    });
  });
}
