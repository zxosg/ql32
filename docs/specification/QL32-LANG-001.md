# QL/32 — Language Baseline

**Document ID:** QL32-LANG-001  
**Status:** M0 baseline

QL/32 BASIC uses SMSQ/E SuperBASIC as its primary reference for immediate
mode, expressions, control flow, procedures/functions, typed suffixes, and
channel-oriented I/O. It implements a documented subset, not blanket source
compatibility.

Public commands are classified as:

| Class | Meaning |
| --- | --- |
| S | Verified SMSQ/E-compatible behavior |
| Q | QL-inspired behavior documented by QL/32 |
| P | Psion-inspired UI interface |
| N | New native QL/32 API |
| X | Explicitly unsupported |

The initial numeric model is `Int32` for `%` variables and IEEE-754 binary64
for floating-point values. String variables use `$`. The M1 executable subset
includes expressions, variables, `PRINT`, `IF`, `FOR`, `REPeat`, `SELect ON`,
and `DEFine PROCedure` / `DEFine FuNction` as individually specified.

