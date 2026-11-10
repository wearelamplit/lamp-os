import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
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
    const cap = Utf8ByteLimitFormatter(kMaxLampNameBytes);

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

    test('a paste over a selected name keeps as much of the paste as fits',
        () {
      final full = 'c' * kMaxLampNameBytes;
      final paste = 'p' * 40;
      final out = cap.formatEditUpdate(
        TextEditingValue(
          text: full,
          selection: TextSelection(baseOffset: 0, extentOffset: full.length),
        ),
        TextEditingValue(
          text: paste,
          selection: TextSelection.collapsed(offset: paste.length),
        ),
      );
      expect(out.text, 'p' * kMaxLampNameBytes);
    });

    test('a paste over a selected name that shares its ending keeps the '
        'start of the paste', () {
      const name = 'Living Room Lamp';
      const paste = "Grandmother's Upstairs Bedroom Lamp";
      final out = cap.formatEditUpdate(
        const TextEditingValue(
          text: name,
          selection: TextSelection(baseOffset: 0, extentOffset: name.length),
        ),
        const TextEditingValue(
          text: paste,
          selection: TextSelection.collapsed(offset: paste.length),
        ),
      );
      expect(out.text, "Grandmother's Upstairs Bedroom L");
    });

    test('a letter typed into a full name leaves the cursor where it was',
        () {
      final full = 'n' * kMaxLampNameBytes;
      final out = cap.formatEditUpdate(
        TextEditingValue(
          text: full,
          selection: const TextSelection.collapsed(offset: 5),
        ),
        TextEditingValue(
          text: 'n' * (kMaxLampNameBytes + 1),
          selection: const TextSelection.collapsed(offset: 6),
        ),
      );
      expect(out.text, full);
      expect(out.selection, const TextSelection.collapsed(offset: 5));
    });

    test('a name leaving its field is fitted to the cap', () {
      expect(fitLampName('か' * 12), 'か' * 10);
      expect(fitLampName('lamp'), 'lamp');
    });

    test('a paste at the start keeps the name and puts the cursor after it',
        () {
      final name = 'n' * 30;
      final out = cap.formatEditUpdate(
        TextEditingValue(
          text: name,
          selection: const TextSelection.collapsed(offset: 0),
        ),
        TextEditingValue(
          text: 'XYZW$name',
          selection: const TextSelection.collapsed(offset: 4),
        ),
      );
      expect(out.text, 'XY$name');
      expect(out.selection, const TextSelection.collapsed(offset: 2));
    });

    TextEditingValue replace(String old, int from, int to, String typed) {
      final text = old.replaceRange(from, to, typed);
      return cap.formatEditUpdate(
        TextEditingValue(
          text: old,
          selection: TextSelection(baseOffset: from, extentOffset: to),
        ),
        TextEditingValue(
          text: text,
          selection: TextSelection.collapsed(offset: from + typed.length),
        ),
      );
    }

    test('an edit next to an emoji never splits it', () {
      // 😀 and 😁 share their first UTF-16 unit; 😀 and 🨀 their second.
      final a28 = 'a' * 28;
      expect(replace('$a28😀', 28, 30, '😁x').text, '$a28😁');
      expect(replace('😀$a28', 0, 2, 'xy🨀').text, 'xy$a28');
    });

    test('a name already past the cap is cut back on the next edit', () {
      final over = 'z' * 40;
      expect(replace(over, 40, 40, 'q').text, 'z' * kMaxLampNameBytes);
    });

    final long = 'か' * 11;
    final composing = TextEditingValue(
      text: long,
      selection: TextSelection.collapsed(offset: long.length),
      composing: TextRange(start: 0, end: long.length),
    );

    test('a word still being composed is capped where the platform caps it '
        '(Android)', () {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;
      addTearDown(() => debugDefaultTargetPlatformOverride = null);
      final out = cap.formatEditUpdate(TextEditingValue.empty, composing);
      expect(out.text, 'か' * 10);
      expect(out.composing, const TextRange(start: 0, end: 10));
    });

    test('a word still being composed waits until it commits where the '
        'platform waits (iOS)', () {
      debugDefaultTargetPlatformOverride = TargetPlatform.iOS;
      addTearDown(() => debugDefaultTargetPlatformOverride = null);
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
