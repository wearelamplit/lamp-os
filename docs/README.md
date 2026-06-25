# Docs

Authoritative refs for the lamp fleet. Read these before changing cross-cutting behavior.

- [`mesh-api.md`](mesh-api.md) — wire-format spec for every ESP-NOW mesh message. Code wins ties; update this doc when it doesn't.
- [`expressions.md`](expressions.md) — developer guide to the lamp's expressions subsystem (auto-triggered animations). How to write a new one, how the wisp-override gate works, the testing pattern.
- [`superpowers/notes/2026-06-10-accepted-security-threats.md`](superpowers/notes/2026-06-10-accepted-security-threats.md) — cleartext-secret threats we are NOT fixing (T1 wispOp setWifi PSK leak, T2 first-claim password) and why. Referenced from `// SECURITY:` comments at the affected call sites.

Design specs and audit reports aren't kept here — once a feature ships the code is truth, and once an audit's items are addressed the value is the diff, not the prose. If you need history, `git log`.
