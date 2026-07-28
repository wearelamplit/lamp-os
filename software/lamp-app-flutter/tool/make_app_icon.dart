// One-shot: composite the Lamplet mark onto an opaque white square for
// flutter_launcher_icons. Run with `dart run tool/make_app_icon.dart`.
import 'dart:io';

import 'package:image/image.dart' as img;

const _canvasSize = 1024;
const _markCoverage = 0.64; // fraction of canvas the mark's long edge fills

void main() {
  final source = img.decodePng(
    File('assets/source/lamplet.png').readAsBytesSync(),
  );
  if (source == null) {
    stderr.writeln('failed to decode assets/source/lamplet.png');
    exit(1);
  }

  final canvas = img.Image(
    width: _canvasSize,
    height: _canvasSize,
    numChannels: 3,
  );
  img.fill(canvas, color: img.ColorRgb8(255, 255, 255));

  final targetLongEdge = (_canvasSize * _markCoverage).round();
  final scale = targetLongEdge / (source.width > source.height ? source.width : source.height);
  final markWidth = (source.width * scale).round();
  final markHeight = (source.height * scale).round();
  final mark = img.copyResize(
    source,
    width: markWidth,
    height: markHeight,
    interpolation: img.Interpolation.average,
  );

  img.compositeImage(
    canvas,
    mark,
    dstX: (_canvasSize - markWidth) ~/ 2,
    dstY: (_canvasSize - markHeight) ~/ 2,
  );

  File('assets/app_icon.png').writeAsBytesSync(img.encodePng(canvas));
  stdout.writeln('wrote assets/app_icon.png (${canvas.width}x${canvas.height}, mark ${markWidth}x$markHeight)');
}
