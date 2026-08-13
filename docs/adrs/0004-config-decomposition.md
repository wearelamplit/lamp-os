# ADR 0004 — Config decomposition: model / codec / store / façade

## Context

`Config` had grown into a ~1100-line megafile owning five distinct
responsibilities at once:

1. the in-memory data model (`LampSettings` / `BaseSettings` / `ShadeSettings`
   / `ExpressionSettings` / `HomeModeSettings`),
2. the JSON codec (the persisted-blob parse, with field defaults, clamps,
   byte-order derivation, and legacy migrations, plus `asJsonDocument`),
3. NVS persistence (a `Preferences*` and all `begin`/`end`/`getString`/
   `putString`/`clear`),
4. the per-section JSON cache that backs the BLE reads, and
5. the dispositions sub-domain (sorted vector + debouncer + eviction).

Two forces pushed on this. NVS access being baked into the data class meant the
parse/serialize logic — the part most likely to harbour a subtle migration bug —
could not be exercised in the native suite at all (the suite keeps itself free
of Arduino/NVS, so it could only test pure helpers or *mirror* logic inline,
which tests a copy, not the real code). And a single class with five reasons to
change is the *large-class* smell (`docs/dev/code-smells.md`).

This is a hardware-validated path (a wrong parse drives the wrong hardware), so
a big-bang rewrite was off the table.

## Decision

Decompose `Config` along its responsibility seams, layered with dependencies
pointing inward, migrating **incrementally behind a stable façade** so call
sites (boot wiring, the Core-1 drains, the webapp) barely move and each step is
independently buildable and bench-validatable:

```
  ConfigData structs (config_types.hpp)        pure
      ▲                    ▲
  ConfigCodec        DispositionStore           pure, native-tested
  (JSON <-> model)   (+ DispositionDebouncer)
      ▲                    ▲
  ConfigStore (interface) <- NvsConfigStore     the only Preferences user
      ▲
  Config (façade)                               owns policy: debounce,
      ▲                                          hash-dedup, section cache
  lamp_drains · main · webapp · ble_control
```

- **`ConfigStore`** is an interface even though `NvsConfigStore` is its only
  production implementation. That is deliberate and not the *speculative
  generality* smell: the in-memory fake is a real second implementation and the
  reason the codec/façade can be tested off-hardware, and the seam inverts the
  dependency on a volatile boundary (NVS). The smell catalog is a set of
  heuristics to judge against, not laws to wield (see `docs/dev/code-smells.md`).
- **`ConfigCodec`** is pure functions over the model structs — no NVS, no
  `Serial` — so the byte-format logic gets real native tests, including a
  serialize→parse round trip.
- **`Config`** stays the façade the rest of the system talks to. It keeps the
  policy (commit/disposition debounce windows, hash-dedup, the section cache,
  the Core-1 single-writer discipline); the mechanism moves below it.

## Alternatives rejected

- **Leave it as one class.** It works and is validated, but the testability gap
  (the highest-risk logic untestable) and the five-responsibility coupling are
  real costs that compound as the config surface grows.
- **Big-bang rewrite into the final shape.** Too much untested change landing at
  once on a validated boot/persistence path. The incremental-behind-a-façade
  path trades a few more commits for a bench checkpoint at each step.
- **Concrete store, no interface.** Gets the dependency-inversion win but leaves
  the testability win — the whole point — on the floor.

## Consequences

- NVS access is now behind one seam; the empty-NVS/corrupt path, factory reset,
  and the lampType/cfg/dispositions keys all route through `ConfigStore`.
- The codec is round-trip tested in the native suite; `config.cpp` drops from
  ~780 to ~500 lines and keeps shrinking as later steps lift the dispositions
  sub-domain and section serializers out.
- The interface adds one level of indirection at the persistence boundary — the
  accepted cost of the test seam and the dependency inversion.
- The migration spans several commits; until it completes, `Config` is part
  façade, part legacy. Each step is behaviour-preserving and must keep the
  native suite green and get a hardware revalidation pass before the branch
  merges.
