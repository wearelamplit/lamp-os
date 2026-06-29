import '../../control/domain/lamp_color.dart';

/// A brightened, washed-toward-white version of [base] for the adopt-confirm
/// identify pulse — recognisably the lamp's own colour, but obviously lit up.
LampColor washedOutBright(LampColor base) {
  int wash(int c) => c + ((255 - c) * 0.5).round(); // 50% toward white
  return LampColor(r: wash(base.r), g: wash(base.g), b: wash(base.b), w: 0);
}
