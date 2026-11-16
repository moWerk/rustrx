# Audits

Each audit is a dated narrative (`<date>-<topic>.md`: scope, method, what
was found, what was done, dead ends) and a findings ledger beside it
(`.findings.json`: one entry per finding with its severity and status,
the commit that fixed it or why it is open). Measured non-findings are
recorded too, so the next audit does not measure them again.

| Date | Topic | Release |
|---|---|---|
| [2026-11-16](2026-11-16-structure.md) | Structure: the whole tree after one week of fast iteration | 0.4.6 |
