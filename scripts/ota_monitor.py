#!/usr/bin/env python3
"""ota_monitor.py — surface mesh-gossip OTA events from a tap log.

Reads a labeled-prefix log (from bench_tap.py, or piped via stdin) and
prints only the lines that matter for an OTA flow: OFFER / ACCEPT /
REQ / DONE / RESULT / abort markers, plus per-MAC version transitions
seen in HELLO recv lines.

Optional --summary mode tracks a running per-MAC version table and
prints a "VERSION mac: old → new" line every time a peer's HELLO
version changes, which is the visible signal of a successful OTA hop.

Usage:
    ota_monitor.py [-f LOG_FILE] [--summary] [--raw]

Examples:
    # Live-tail the bench log, OTA events only
    ota_monitor.py -f /tmp/bench.log

    # Same, but also flag every version transition
    ota_monitor.py -f /tmp/bench.log --summary

    # Pipe from bench_tap.py directly (stdout streaming)
    scripts/bench_tap.py /dev/cu.usbserial-0001:flora \\
                         /dev/cu.usbserial-6:gramp \\
                       | scripts/ota_monitor.py --summary

    # Pass through everything (debug / context)
    ota_monitor.py -f /tmp/bench.log --raw
"""

import argparse
import os
import re
import sys
import time

# Lines that matter for the OTA cascade story. Anything matching one of
# these patterns gets surfaced.
OTA_PATTERNS = [
    re.compile(r"consider [0-9A-Fa-f:]{17} v=.*→ OFFER"),
    re.compile(r"\bOFFER -> [0-9A-Fa-f:]{17} v="),
    re.compile(r"\bOFFER pipelined erase"),
    re.compile(r"\bOFFER from [0-9A-Fa-f:]{17}"),
    re.compile(r"\bACCEPT from"),
    re.compile(r"\bACCEPT drop:"),
    re.compile(r"\bDONE -> [0-9A-Fa-f:]{17}"),
    re.compile(r"\bRESULT (success|failure)"),
    re.compile(r"\bFINALIZE timeout"),
    re.compile(r"\bREQ first=\d+"),
    re.compile(r"\bREQ flood:"),
    re.compile(r"\bREQ served"),
    re.compile(r"\bstream progress: sent"),
    re.compile(r"\brecv progress: \d+ chunks"),
    re.compile(r"\baborting to Idle"),
    re.compile(r"\bhard cap exceeded"),
    re.compile(r"\bchunk drop: not armed"),
    re.compile(r"\bcfg\] loaded name="),
    re.compile(r"\bsoftAP up ssid="),
    re.compile(r"\bpaused adv \+ scan"),
    re.compile(r"\bresumed adv \+ scan"),
]

HELLO_PATTERN = re.compile(
    r"\[(?P<label>[^\]]+)\] \[show\] HELLO recv from "
    r"(?P<mac>[0-9A-Fa-f:]{17}) v=0x(?P<ver>[0-9A-Fa-f]+)")


def is_ota(line: str) -> bool:
    return any(p.search(line) for p in OTA_PATTERNS)


def iter_tail(path: str):
    """Yield new lines appended to `path`, polling forever (tail -F)."""
    with open(path, "r") as f:
        f.seek(0, os.SEEK_END)
        while True:
            line = f.readline()
            if line:
                yield line
            else:
                time.sleep(0.1)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Surface OTA events from a bench-tap log",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument(
        "-f", "--file",
        help="log file to tail (omit to read stdin)")
    ap.add_argument(
        "--summary", action="store_true",
        help="emit 'VERSION mac: old → new' on each HELLO version transition")
    ap.add_argument(
        "--raw", action="store_true",
        help="also pass through non-OTA lines (preserves full context)")
    args = ap.parse_args()

    if args.file:
        src = iter_tail(args.file)
    else:
        src = sys.stdin

    versions: dict[str, str] = {}

    try:
        for line in src:
            line = line.rstrip()

            if args.summary:
                m = HELLO_PATTERN.search(line)
                if m:
                    mac = m.group("mac")
                    ver = m.group("ver")
                    prev = versions.get(mac)
                    if prev != ver:
                        print(f"== VERSION {mac}: {prev or '(new)'} → {ver}",
                              flush=True)
                        versions[mac] = ver

            if is_ota(line):
                print(line, flush=True)
            elif args.raw:
                print(line, flush=True)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
