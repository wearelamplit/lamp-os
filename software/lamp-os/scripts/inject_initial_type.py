#!/usr/bin/env python3
"""PIO pre-build hook: inject LAMP_INITIAL_TYPE build define from the
env's `custom_lamp_variant` option.

Each per-variant env in platformio.ini declares its identity:
  [env:upesy_wroom_standard]
  custom_lamp_variant = standard

This hook reads that key via env.GetProjectOption() and adds
-D LAMP_INITIAL_TYPE="standard" (as a C string literal) to the build
flags. The lamp's main.cpp factory uses this value on first boot to
seed NVS lampType (after which OTAs preserve identity).

When the env doesn't declare `custom_lamp_variant` (e.g. the native
test env), no flag is injected — main.cpp's `#ifndef LAMP_INITIAL_TYPE`
falls back to empty string.

Wired from software/lamp-os/platformio.ini as:
  extra_scripts =
    pre:scripts/inject_initial_type.py

Empty/unset LAMP_TYPE = no flag injected (default empty string in
main.cpp's `#ifndef LAMP_INITIAL_TYPE` fallback).

Bypasses shell-quoting hazards.
"""

Import("env")  # SCons / PIO global

try:
    variant = env.GetProjectOption("custom_lamp_variant")
except Exception:
    variant = None

if variant:
    variant = str(variant).strip()

if variant:
    # The C string literal must arrive at the compiler with quotes. The
    # CPPDEFINES tuple form `(NAME, value)` lets SCons handle the quoting.
    # We pass the value with escaped quotes so it reaches GCC as a string
    # literal, not an identifier.
    env.Append(CPPDEFINES=[("LAMP_INITIAL_TYPE", '\\"' + variant + '\\"')])
    print(f"[inject_initial_type] LAMP_INITIAL_TYPE={variant}")
else:
    print("[inject_initial_type] custom_lamp_variant not set; "
          "no build flag injected")
