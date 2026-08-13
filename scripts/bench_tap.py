#!/usr/bin/env python3
"""bench_tap.py — multi-port serial tail with labeled prefixes.

Tails one or more lamp serial ports concurrently, prefixing every line
with a short label so cross-lamp behavior (cascade OTA, mesh paint,
greet handshakes) is readable in one stream. Designed for bench
diagnostics where two or three lamps + a wisp are USB-connected and you
want a single timestamped-ish log of who said what.

Replaces the local-only /tmp/dual_tap.py historically used during
mesh-OTA bring-up.

Usage:
    bench_tap.py PORT[:LABEL] [PORT[:LABEL] ...] [-o LOG_FILE]

Examples:
    # Tail two lamps + the wisp to stdout
    bench_tap.py /dev/cu.usbserial-0001:flora \\
                 /dev/cu.usbserial-6:gramp \\
                 /dev/cu.usbmodem11101:wisp

    # Send to a log file you can grep / Monitor-tool tail later
    bench_tap.py -o /tmp/bench.log \\
                 /dev/cu.usbserial-0001:flora \\
                 /dev/cu.usbserial-6:gramp

If LABEL is omitted, the basename of the port is used (e.g.
'/dev/cu.usbserial-6' -> 'usbserial-6').

The script holds DTR high + RTS low after opening so the lamps don't
auto-reset on connection (matches the upesy_wroom auto-reset circuit).
On READ FAIL (lamp reboots, USB hub blips), the affected thread
auto-reopens after a 1 s delay.
"""

import argparse
import serial
import sys
import threading
import time

BAUD = 115200


def tail(port: str, label: str, outf, lock: threading.Lock,
         stop_evt: threading.Event) -> None:
    """Read serial port forever, write prefixed lines to outf.

    Reopens the port on any read failure (covers lamp reboots, USB blips).
    """
    while not stop_evt.is_set():
        try:
            s = serial.Serial(port, BAUD, timeout=0.5)
            s.setDTR(True)
            s.setRTS(False)
        except Exception as e:
            with lock:
                outf.write(f"[{label}] OPEN FAIL: {e}\n")
                outf.flush()
            time.sleep(2.0)
            continue

        buf = b""
        try:
            while not stop_evt.is_set():
                try:
                    chunk = s.read(4096)
                except Exception as e:
                    with lock:
                        outf.write(f"[{label}] READ FAIL (reopening): {e}\n")
                        outf.flush()
                    break
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode(errors="replace").rstrip()
                    with lock:
                        outf.write(f"[{label}] {text}\n")
                        outf.flush()
        finally:
            try:
                s.close()
            except Exception:
                pass
        if not stop_evt.is_set():
            time.sleep(1.0)  # brief pause before reopen


def parse_target(spec: str) -> tuple[str, str]:
    """Parse 'PORT[:LABEL]'. Defaults LABEL to basename(PORT)."""
    if ":" in spec:
        port, label = spec.split(":", 1)
    else:
        port = spec
        label = port.rsplit("/", 1)[-1]
    return port, label


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Tail multiple serial ports with labeled prefixes",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument(
        "-o", "--output", default="-",
        help="output file (default: stdout). Pass '-' for stdout.")
    ap.add_argument(
        "ports", nargs="+", metavar="PORT[:LABEL]",
        help="serial ports to tail; optionally suffix :LABEL")
    args = ap.parse_args()

    targets = [parse_target(s) for s in args.ports]

    outf = sys.stdout if args.output == "-" else open(args.output, "w")
    try:
        outf.write(f"=== bench_tap start {time.strftime('%H:%M:%S')} "
                   f"({len(targets)} ports) ===\n")
        outf.flush()

        stop_evt = threading.Event()
        lock = threading.Lock()
        threads = [
            threading.Thread(target=tail,
                             args=(port, label, outf, lock, stop_evt),
                             daemon=True)
            for port, label in targets
        ]
        for t in threads:
            t.start()

        try:
            while True:
                time.sleep(60)
        except KeyboardInterrupt:
            stop_evt.set()
            for t in threads:
                t.join(timeout=2)
    finally:
        if outf is not sys.stdout:
            outf.write(f"=== bench_tap stop {time.strftime('%H:%M:%S')} ===\n")
            outf.close()


if __name__ == "__main__":
    main()
