import '../../../core/utils/string_case.dart';
import '../../control/application/control_state.dart';
import '../domain/firmware_state.dart';

/// Every line the app shows its owner about a firmware push, in the lamp's
/// "new tricks" voice. `name` is the lamp's display name from [nameOf]; a
/// line that opens with it is capitalized, so the fallback reads right.
abstract class FirmwareCopy {
  /// The lamp's name as the app shows it, or "your lamp" before it loads.
  static String nameOf(ControlState? state) =>
      state?.lamp.name.toTitleCase() ?? 'your lamp';

  static String teaching(String name) => 'Teaching $name new tricks 🎓';

  static String settling(String name) => 'Giving $name a moment to settle… 🎓';

  static String learned(String name) =>
      _sentence('$name learned some new tricks ✨');

  static String missed(String name) => _sentence(
    "$name didn't catch the new tricks this time. "
    'Trying again next time you connect.',
  );

  static String alreadyKnows(String name) =>
      _sentence('$name already knows these tricks.');

  static String failure(FirmwareFailure failure, String name) =>
      _sentence(switch (failure) {
        FirmwareFailure.noAnswer =>
          "$name didn't answer. Bring your phone closer and try again.",
        FirmwareFailure.setupFailed =>
          "Couldn't start the lesson. Check $name is still connected and "
              'try again.',
        FirmwareFailure.busy =>
          '$name is already learning from another lamp. Give it a few '
              'minutes.',
        FirmwareFailure.untrustedAtOffer || FirmwareFailure.untrusted =>
          "$name didn't trust that download, so it passed. Delete it and let "
              'the app fetch a fresh copy.',
        FirmwareFailure.linkLost =>
          'Lost $name partway through. Bring your phone closer and try '
              'again.',
        FirmwareFailure.missingPieces =>
          '$name kept missing pieces. Try again from closer by.',
        FirmwareFailure.noResult =>
          '$name went quiet before saying how it went. If the version below '
              "hasn't changed in a minute, try again.",
        FirmwareFailure.scrambled =>
          'Something got scrambled on the way to $name. Try again.',
        FirmwareFailure.storeFailed =>
          "$name couldn't store the new tricks. Try again. If it keeps "
              'happening, the lamp needs a USB flash.',
        FirmwareFailure.unknown =>
          "$name couldn't learn the new tricks. Try again.",
        FirmwareFailure.cacheMissing =>
          'That download has gone missing. It comes back next time the app '
              'starts online.',
        FirmwareFailure.cacheUnverified =>
          "That download doesn't check out. Delete it and let the app fetch "
              'a fresh copy.',
        FirmwareFailure.wrongLamp =>
          'That download is for a different kind of lamp.',
        FirmwareFailure.cancelled =>
          'Lesson cancelled. $name stops waiting for the rest in about a '
              'minute.',
      });

  static String _sentence(String s) =>
      s.isEmpty ? s : s[0].toUpperCase() + s.substring(1);
}
