#!/usr/bin/env bash
# Run flutter with --dart-define=APP_COMMIT=<short sha>[-dirty] so the Info
# screen can name the build.
# Usage: flutter-with-commit.sh <flutter run|build ...> (the flag is appended)
set -e
dir=$(dirname "$0")
sha=$(git -C "$dir" rev-parse --short HEAD)
[ -z "$(git -C "$dir" status --porcelain)" ] || sha="$sha-dirty"
exec flutter "$@" "--dart-define=APP_COMMIT=$sha"
