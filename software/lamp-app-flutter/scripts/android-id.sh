#!/usr/bin/env bash
# Print the id of the first connected Android device, or exit nonzero.
set -e
id=$(flutter devices --machine 2>/dev/null \
  | python3 -c "import json,sys; print(next((d['id'] for d in json.load(sys.stdin) if d.get('targetPlatform','').startswith('android')), ''))")
if [ -z "$id" ]; then
  echo "no Android device — plug in phone or start emulator" >&2
  exit 1
fi
echo "$id"
