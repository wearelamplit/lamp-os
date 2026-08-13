#!/usr/bin/env python3
"""bench_cmd.py — send serial commands to a lamp and stream the reply.

Opens one lamp serial port read+write (115200), sends newline-terminated
command lines (from --cmd args and/or stdin), then streams every output
line with a host timestamp until --duration elapses or --until matches.

The lamp-side ingress is LAMP_DEBUG-gated (beta/dev channel builds only;
stable firmware ignores serial input). Command grammar:

    {"a":"test_expression","type":"pulse","target":3}   lamp-action JSON
    expr.get                                            print expressions section JSON
    expr.set <json>                                     apply expressions section JSON
                                                        (array of entries, or a single
                                                        {"op":...} object)

Every command is ACKed on the lamp's serial output as '[cmd] ok ...' or
'[cmd] err <reason>'.

The port is exclusive: STOP any bench_tap.py tailing the same port
first, or the two fight over reads and both see torn lines.

Usage:
    bench_cmd.py PORT [--cmd LINE ...] [--stdin] [-o LOG_FILE]
                 [--duration N] [--until REGEX]

Examples:
    # Trigger a pulse test, watch 10 s of output
    bench_cmd.py /dev/cu.usbserial-6 \\
        --cmd '{"a":"test_expression","type":"pulse","target":3}' \\
        --duration 10

    # Read back stored expressions, exit on the ACK
    bench_cmd.py /dev/cu.usbserial-6 --cmd expr.get \\
        --until '\\[cmd\\] (ok|err)' --duration 5

    # Pipe a command script
    printf 'expr.get\\n' | bench_cmd.py /dev/cu.usbserial-6 --stdin --duration 5

Exits 0 on --duration expiry or --until match; exits 1 when --until was
given but never matched before the deadline / Ctrl-C.
"""

import argparse
import re
import serial
import sys
import time

BAUD = 115200


def ts() -> str:
    now = time.time()
    return time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Send serial commands to a lamp and stream the reply",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    ap.add_argument("port", help="serial port (exclusive: stop bench_tap on it first)")
    ap.add_argument(
        "--cmd", action="append", default=[], metavar="LINE",
        help="command line to send (repeatable, sent in order)")
    ap.add_argument(
        "--stdin", action="store_true",
        help="also read command lines from stdin (after --cmd lines)")
    ap.add_argument(
        "-o", "--output", default=None,
        help="also append timestamped output to this file")
    ap.add_argument(
        "--duration", type=float, default=None, metavar="N",
        help="exit after N seconds (default: run until Ctrl-C or --until match)")
    ap.add_argument(
        "--until", default=None, metavar="REGEX",
        help="exit as soon as an output line matches REGEX")
    ap.add_argument(
        "--wait-ready", default=None, metavar="REGEX",
        help="hold commands until an output line matches REGEX "
             "(use when the port open resets the lamp); waits at most "
             "--wait-ready-timeout seconds, then sends anyway")
    ap.add_argument(
        "--wait-ready-timeout", type=float, default=15.0, metavar="N",
        help="max seconds to wait for --wait-ready (default 15)")
    ap.add_argument(
        "--reset", action="store_true",
        help="pulse EN (RTS) after open to force a clean boot; pair with "
             "--wait-ready. Serial RX has been seen dead on no-reset opens, "
             "so a deterministic reset beats hoping.")
    args = ap.parse_args()

    until_re = re.compile(args.until) if args.until else None

    cmds = list(args.cmd)
    if args.stdin:
        cmds += [ln.rstrip("\n") for ln in sys.stdin if ln.strip()]

    # Set the line state BEFORE open — pyserial pulses DTR/RTS during
    # open() otherwise, which resets the lamp (upesy_wroom auto-reset
    # circuit). Belt and braces: --wait-ready covers ports that pulse
    # at the OS level regardless.
    # esptool-style line state: both deasserted while running (asserted DTR
    # with deasserted RTS pins IO0 low on the upesy auto-reset circuit and
    # has been seen killing UART RX).
    s = serial.Serial(None, BAUD, timeout=0.2)
    s.port = args.port
    s.dtr = False
    s.rts = False
    s.open()
    s.dtr = False
    s.rts = False
    if args.reset:
        s.rts = True   # EN low (DTR stays deasserted so IO0 stays high)
        time.sleep(0.1)
        s.rts = False  # EN high, boot to app
        s.reset_input_buffer()

    outf = open(args.output, "a") if args.output else None

    def emit(line: str) -> None:
        text = f"{ts()} {line}\n"
        sys.stdout.write(text)
        sys.stdout.flush()
        if outf:
            outf.write(text)
            outf.flush()

    matched = False
    deadline = time.time() + args.duration if args.duration is not None else None
    try:
        if args.wait_ready:
            ready_re = re.compile(args.wait_ready)
            ready_deadline = time.time() + args.wait_ready_timeout
            rbuf = b""
            while time.time() < ready_deadline:
                rbuf += s.read(4096)
                while b"\n" in rbuf:
                    line, rbuf = rbuf.split(b"\n", 1)
                    text = line.decode(errors="replace").rstrip()
                    emit(text)
                    if ready_re.search(text):
                        break
                else:
                    continue
                break
            else:
                emit(f"-- wait-ready timeout ({args.wait_ready_timeout}s), sending anyway")

        for c in cmds:
            # Host-side pause pseudo-command: --cmd sleep:12 sends nothing
            # and drains output for 12 s (roster fill, animation windows).
            m = re.match(r"sleep:(\d+(?:\.\d+)?)$", c)
            if m:
                emit(f"-- sleep {m.group(1)}s")
                t_end = time.time() + float(m.group(1))
                sbuf = b""
                while time.time() < t_end:
                    sbuf += s.read(4096)
                    while b"\n" in sbuf:
                        line, sbuf = sbuf.split(b"\n", 1)
                        emit(line.decode(errors="replace").rstrip())
                continue
            s.write(c.encode() + b"\n")
            emit(f">> {c}")
            # Persist-heavy commands (expr.set) stall the lamp loop; pace so
            # the next line can't land while the RX ring is unserviced.
            time.sleep(0.25)

        buf = b""
        while not matched:
            if deadline is not None and time.time() >= deadline:
                break
            chunk = s.read(4096)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip()
                emit(text)
                if until_re and until_re.search(text):
                    matched = True
                    break
    except KeyboardInterrupt:
        pass
    finally:
        s.close()
        if outf:
            outf.close()

    if until_re and not matched:
        sys.exit(1)


if __name__ == "__main__":
    main()
