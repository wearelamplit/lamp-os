# Code smells

Heuristics to investigate, not rules. Sometimes the right call is "leave it."

## Speculative generality
Abstraction with no current consumer: interface-with-one-impl, factory for one
product, a "future override" hook, a reserved enum slot, a template for
hypothetical reuse.
**Fine when** the second impl is a test fake, or it inverts a dependency on a
volatile boundary (NVS, network, clock) so callers test without it.
**Fix:** inline the one path, or land the seam with its first consumer.

## Large class / long function
One unit with many reasons to change (parsing + persistence + caching).
**Fine when** genuinely cohesive. **Fix:** split along responsibility seams
behind a façade so call sites don't move.

## Hardcoded boundary access
NVS / network / `Serial` / filesystem calls scattered through domain logic.
**Fix:** route through one store / transport / clock seam.

## Duplicated logic
The same rule in two places, free to drift. **Fine when** the likeness is
coincidental. **Fix:** one named source the copies share.

## Magic numbers / primitive obsession
A bare literal carrying meaning a name should: `if (px == 38)`, a mode as raw
`uint8_t`. **Fix:** named constant or enum.

## Comment-as-deodorant
A comment whose job is to make confusing code readable. **Fix:** fix the code so
it isn't needed.

## Flag parameter
`render(true)` — a bool/mode that hides what it toggles, usually two functions in
one. **Fine when** an obvious pass-through. **Fix:** two functions, or an enum.

## Shotgun surgery
One change touches many files (a new field → struct, parser, serializer, tests).
**Fix:** the missing seam/owner that should localize it.

## Feature envy
A function more interested in another type's internals than its own.
**Fix:** move the behavior to the data.
