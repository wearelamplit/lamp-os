#!/usr/bin/env bash
# OTA chunk-size bench: flash a fleet (receivers @1.1.1 + 1 sender @1.1.2) at a
# given FW_CHUNK_SIZE_MAX, capture the cascade, parse per-target time + REQ.
#
# Usage: ota_size_test.sh <SIZE> <captureSeconds> <SENDER_PORT> <RECEIVER_PORT>...
#   Caller determines the ports (all standard, same channel). Map port->MAC with:
#     python3 -m esptool --port <PORT> --chip esp32 --after hard_reset read_mac
#   SENDER gets v1.1.2, every RECEIVER gets v1.1.1, so the sender (+ cascade) OTAs them.
set -euo pipefail
usage(){ echo "usage: $0 <SIZE> <captureSeconds> <SENDER_PORT> <RECEIVER_PORT>..." >&2; exit 2; }
SIZE="${1:-}"; SECS="${2:-}"
[ -n "$SIZE" ] && [ -n "$SECS" ] || usage
shift 2
SENDER="${1:-}"; [ -n "$SENDER" ] || usage; shift
RECEIVERS=("$@")
[ "${#RECEIVERS[@]}" -ge 1 ] || { echo "error: need at least one receiver port" >&2; usage; }
WT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
LAMP="$WT/software/lamp-os"
FW="$LAMP/src/components/network/protocol/fw_ota.hpp"
SIGNED="$LAMP/.pio/build/upesy_wroom_standard/firmware-signed.bin"
ALL=("$SENDER" "${RECEIVERS[@]}")
ESPT="python3 -m esptool"
cd "$WT"

# SIZE may carry a non-numeric label suffix (e.g. "1444att") for log naming;
# the C++ constant needs the numeric part only.
SIZENUM="${SIZE%%[!0-9]*}"
[ -n "$SIZENUM" ] || { echo "error: SIZE must start with a number" >&2; exit 2; }
echo "== set FW_CHUNK_SIZE_MAX = $SIZENUM (label=$SIZE) =="
sed -i '' -E "s/(FW_CHUNK_SIZE_MAX[[:space:]]*=[[:space:]]*)[0-9]+/\1$SIZENUM/" "$FW"
grep -n 'FW_CHUNK_SIZE_MAX ' "$FW" | head -1

build() { # $1 = version
  printf '%s' "$1" > "$WT/VERSION"
  rm -rf "$LAMP/.pio/build/upesy_wroom_standard" "$LAMP/.buildcache"
  local flags="-D LAMP_DEBUG"
  [ -n "${TX_QDBM:-}" ] && flags="$flags -D LAMP_ESPNOW_TX_QDBM=$TX_QDBM"
  ( cd "$LAMP" && LAMP_FIRMWARE_CHANNEL=beta PLATFORMIO_BUILD_FLAGS="$flags" pio run -e upesy_wroom_standard ) >/dev/null 2>&1
  [ -f "$SIGNED" ] || { echo "BUILD FAIL v$1"; exit 1; }
}
flash() { # $1 = port — retries: esptool sync hiccups are transient
  local i
  for i in 1 2 3; do
    if $ESPT --port "$1" --chip esp32 --baud 921600 write_flash 0x10000 "$SIGNED" >/dev/null 2>&1 \
       && $ESPT --port "$1" --chip esp32 --baud 921600 erase_region 0xE000 0x2000 >/dev/null 2>&1; then
      echo "  flashed $1"; return 0
    fi
    echo "  flash retry $i/3 on $1"; sleep 2
  done
  echo "  FLASH FAILED on $1 after 3 tries"; return 1
}

echo "== build receiver image v1.1.1 @${SIZE} =="; build 1.1.1
echo "== flash 3 receivers =="; for p in "${RECEIVERS[@]}"; do flash "$p"; done
echo "== build sender image v1.1.2 @${SIZE} =="; build 1.1.2
echo "== flash sender =="; flash "$SENDER"
printf '1.1.2' > "$WT/VERSION"

echo "== reset all + capture ${SECS}s =="
for p in "${ALL[@]}"; do $ESPT --port "$p" --chip esp32 --after hard_reset read_mac >/dev/null 2>&1 || true; done
python3 - "$SIZE" "$SECS" "${ALL[@]}" <<'PY'
import serial, time, threading, sys
size=sys.argv[1]; secs=int(sys.argv[2]); ports=sys.argv[3:]
def opn(p):
    s=serial.Serial(); s.port=p; s.baudrate=115200; s.timeout=0.1; s.open(); s.setDTR(True); s.setRTS(False); return s
sers={p:opn(p) for p in ports}
end=time.time()+secs
outs={p:open(f'/tmp/sweep_{size}_{p.split(".")[-1]}.log','w') for p in ports}
def tail(p,s,f):
    while time.time()<end:
        try: line=s.readline().decode('utf-8','replace')
        except: break
        if line.strip(): f.write(f'{time.time():.2f} {line}'); f.flush()
ts=[threading.Thread(target=tail,args=(p,sers[p],outs[p])) for p in ports]
for t in ts: t.start()
for t in ts: t.join()
for p in ports: sers[p].close(); outs[p].close()
print(f'captured {secs}s at size {size}')
PY
echo "== DONE size $SIZE =="
