#!/usr/bin/env python3
"""Generate the per-variant expression catalog headers into committed flash.

Compiles test/expr_catalog_gen/gen_main.cpp on the host (no Arduino) and runs
it, writing software/lamp-os/generated/<variant>/expr_catalog.h. Idempotent:
re-running produces no git diff unless a descriptor or the variant subset
changed. Wired to `npm run catalog:gen`.
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAMP = os.path.join(ROOT, "software", "lamp-os")


def find_arduinojson():
    hits = glob.glob(os.path.join(LAMP, ".pio", "libdeps", "*", "ArduinoJson", "src"))
    return hits[0] if hits else None


def main():
    aj = find_arduinojson()
    if not aj:
        subprocess.check_call(["pio", "pkg", "install", "-e", "native"], cwd=LAMP)
        aj = find_arduinojson()
    if not aj:
        sys.exit("ArduinoJson host headers not found; run `pio test -e native` once to fetch deps")

    shared = os.path.join(ROOT, "software", "shared")
    out_bin = os.path.join(LAMP, ".pio", "catalog_gen")
    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    subprocess.check_call([
        os.environ.get("CXX", "g++"), "-std=gnu++2a",
        "-I", os.path.join(LAMP, "src"),
        "-I", os.path.join(LAMP, "test", "native_stubs"),
        "-I", os.path.join(shared, "protocol", "src"),
        "-I", os.path.join(shared, "led-common", "src"),
        "-I", os.path.join(shared, "crypto", "src"),
        "-I", aj,
        os.path.join(LAMP, "test", "expr_catalog_gen", "gen_main.cpp"),
        "-o", out_bin,
    ])
    subprocess.check_call([out_bin, os.path.join(LAMP, "generated")])
    print("wrote", os.path.join(LAMP, "generated", "<variant>", "expr_catalog.h"))


if __name__ == "__main__":
    main()
