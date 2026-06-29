# Code smells

Heuristics, not laws. Each entry is a prompt to look closer, not an automatic
fix — and the right fix is sometimes "leave it." A smell flags *possible* debt;
you still have to judge whether it's costing anything here. Cite a smell to
start a conversation, never to end one ("that's speculative generality, delete
it" is an argument, not a verdict).

The recurring trap is treating a heuristic as a rule. "No interface with one
implementation" reads clean until the one interface is the seam your tests
mock — then the rule is wrong and the smell never applied. Read each entry as
"investigate when," not "forbid."

## Speculative generality

Abstraction with no current consumer: an interface with one implementation, a
factory for one product, a virtual hook "for future override", a reserved enum
slot or capability bit, a template helper introduced for hypothetical reuse.

**Often fine when** the abstraction pays off *today*, not hypothetically:
- The "second implementation" is a **test fake**. A real impl + an in-memory
  fake is two implementations; the interface is a test seam, not speculation.
- It **inverts a dependency on a volatile/external boundary** (NVS, network,
  clock, filesystem) so higher-level code compiles and tests without dragging
  the boundary in. The payoff is the decoupling, not future swappability.
- It's a deliberate **port** keeping a domain layer free of an adapter.

`FirmwareTransport` and the OTA `fsHooks` are interfaces in this codebase for
exactly those reasons. The smell is real only when the justification is "might
need it later" with no test and no boundary.

**Fix:** delete it and inline the one path; *or*, if it's a test/boundary seam,
land it with its first consumer (the test or the injection site) in the same
change so it's never abstraction-in-waiting.

## Large class / long function

One unit owning many reasons to change. A file you scroll for a minute; a class
that does parsing *and* persistence *and* caching.

**Often fine when** it's genuinely cohesive (a parser that's just long). Look
closer when you can name three unrelated reasons it would change — that's three
responsibilities wanting their own homes.

**Fix:** split along the responsibility seams, behind a façade that keeps call
sites stable so you can move incrementally.

## Hardcoded boundary access

Direct NVS / network / `Serial` / filesystem calls scattered through domain
logic instead of behind one seam. Couples the logic to the platform and blocks
native testing.

**Fix:** route all access through a single store/transport/clock seam. (See the
boundary-seam case under *Speculative generality* — this is its other half.)

## Duplicated logic

The same rule expressed in more than one place, free to drift. The danger isn't
the typing, it's the day someone updates one copy.

**Often fine when** the similarity is coincidental — two things that look alike
but aren't the *same* rule. Don't fuse them; they'll diverge for good reasons.

**Fix:** one named source (constant, function) the copies reference.

## Magic numbers / primitive obsession

A bare literal or string carrying meaning a name should carry: `if (px == 38)`,
a status passed as `uint8_t`, a `0`/`1`/`2` mode.

**Fix:** named constant or enum. If a comment exists *only* to explain what a
literal means, that's the signal — make it a name, not a comment.

## Comment-as-deodorant

A comment whose job is to make confusing code readable. The smell is the
confusion; the comment masks it.

**Fix:** fix the code (rename, extract, simplify) so the comment isn't needed.
Keep comments for the genuinely non-obvious WHY (see the comment policy in
`CLAUDE.md`).

## Flag parameter

`render(true)` / `write(data, false)` — a boolean (or mode int) at the call site
that forces the reader to go look up what it toggles, and usually means the
function does two things.

**Often fine when** it's a genuine pass-through option with an obvious name at
the call site. Look closer when the flag selects between two behaviors.

**Fix:** two named functions, or an enum argument that reads at the call site.

## Shotgun surgery

One conceptual change forces edits across many files. Adding a field touches the
struct, the parser, the serializer, three call sites, and a test.

**Fix:** find the missing seam or owner that should have localized the change.
(Often the inverse of a large class — the responsibility is smeared, not piled.)

## Feature envy / inappropriate intimacy

A function that reaches deep into another type's internals to do its work — more
interested in someone else's data than its own.

**Fix:** move the behavior to the data it envies.
