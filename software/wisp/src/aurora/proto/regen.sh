#!/usr/bin/env bash
set -euo pipefail
# Regenerates src/proto/*.pb.{c,h} from aurora_notifications.proto.
# Requires nanopb_generator (pip install nanopb, or use the one PlatformIO
# downloads under ~/.platformio/packages/tool-nanopb/generator).
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../src/proto"
mkdir -p "$OUT"

# Prefer the repo-local venv generator, then PATH, then the PlatformIO-bundled one.
if [ -x "$HERE/../.venv/bin/nanopb_generator" ]; then
  GEN="$HERE/../.venv/bin/nanopb_generator"
elif command -v nanopb_generator >/dev/null 2>&1; then
  GEN="nanopb_generator"
else
  GEN="$(find "${HOME}/.platformio/packages/tool-nanopb" -name nanopb_generator.py 2>/dev/null | head -1)"
  [ -n "$GEN" ] || { echo "nanopb_generator not found (try: .venv/bin/pip install nanopb)"; exit 1; }
  GEN="python3 $GEN"
fi

$GEN -I "$HERE" -D "$OUT" aurora_notifications.proto
echo "Generated: $OUT/aurora_notifications.pb.{c,h}"
