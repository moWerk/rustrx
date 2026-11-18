# Audits

Each audit is a dated narrative (`<date>-<topic>.md`: scope, method, what
was found, what was done, dead ends) and a findings ledger beside it
(`.findings.json`: one entry per finding with its severity and status,
the commit that fixed it or why it is open). Measured non-findings are
recorded too, so the next audit does not measure them again.

| Date | Topic | Release |
|---|---|---|
| [2026-11-16](2026-11-16-structure.md) | Structure: the whole tree after one week of fast iteration | 0.4.6 |
| [2026-11-16](2026-11-16-lifecycle-and-input.md) | Lifecycle and untrusted input, after the first tests | 0.4.7 |
| [2026-11-18](2026-11-18-memory.md) | Memory: glibc arenas and the store pools, after a crash report | 0.4.9 |
