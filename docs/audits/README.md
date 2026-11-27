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
| [2026-11-19](2026-11-19-calls-and-lifecycle.md) | Calls, lifecycle and the night's work: the abort at every quit since 0.4.7 | 0.6 work |
| [2026-11-22](2026-11-22-performance.md) | Performance and memory: startup, scrolling, idle and media, measured on two phones | 0.8.2 |
| [2026-11-27](2026-11-27-security.md) | Security: other apps, network and logins, secrets, the C ABI, content from others | 0.8.4 |
