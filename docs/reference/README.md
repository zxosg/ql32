# Language reference sources

These are the sources linked in the “QL32 Project Idea” chat and used to compile [QL32-LANG-001](../specification/QL32-LANG-001.md). The manuals are linked rather than redistributed. *Programming Psion Computers* explicitly reserves reproduction rights.

## SuperBASIC / SMSQ/E

- [SBASIC/SuperBASIC Reference Manual Online](https://superbasic-manual.readthedocs.io/en/latest/) (online edition 4.0.2). It includes QL ROM, SMSQ/E, and optional toolkit commands. Check each command's **Location** and **SMS NOTES** before claiming SMSQ/E compatibility.
- [Original Sinclair QL User Guide](https://sinclairql.speccy.org/archivo/docs/sys/QL_UsersGuide.pdf), “keyword” section in the Concepts guide: mixed-case keyword notation marks the minimum accepted abbreviation; entered keywords are reproduced in full. It gives `REPeat` → `REP` as an example.
- [IF](https://superbasic-manual.readthedocs.io/en/latest/I/if.html): inline and block syntax; conditions and Boolean results use true/false and numeric `1`/`0`.
- [FOR](https://superbasic-manual.readthedocs.io/en/latest/F/for.html) and [END FOR](https://superbasic-manual.readthedocs.io/en/latest/E/end--for.html): inline and block forms, mixed lists of single values and stepped intervals, `NEXT`, and `EXIT`.
- [REPeat](https://superbasic-manual.readthedocs.io/en/latest/R/repeat.html), [END REPeat](https://superbasic-manual.readthedocs.io/en/latest/E/end--repeat.html), [NEXT](https://superbasic-manual.readthedocs.io/en/latest/N/next.html), and [EXIT](https://superbasic-manual.readthedocs.io/en/latest/E/exit.html): named loop syntax and loop-control behavior.
- [END SELect](https://superbasic-manual.readthedocs.io/en/working/E/end--select.html) and [The Sinclair QL Series: QL SuperBASIC and its Applications](https://trastero.speccy.org/cosas/Libros/QL/PDF/The_Sinclair_QL_Series_QL_SuperBASIC_and_its_Applications_%5BEN%5D.pdf): `SELect ON`, `= value`, `= start TO end`, `= REMAINDER`, and block termination. The implementation supports the block form; its exact singleton matching remains QL/32-specific pending tests against SBASIC.
- [DEFine PROCedure](https://superbasic-manual.readthedocs.io/en/latest/D/define--procedure.html), [END DEFine](https://superbasic-manual.readthedocs.io/en/latest/E/end--define.html), and [LOCal](https://superbasic-manual.readthedocs.io/en/latest/L/local.html): calls, returns, local variables.
- [DIV](https://superbasic-manual.readthedocs.io/en/latest/D/div.html): floor quotient and paired `MOD` behavior.
- [DIM](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html): numeric arrays have zero-based, inclusive upper bounds; historical string arrays have additional fixed-length semantics.
- [A11 Mathematics](https://superbasic-manual.readthedocs.io/en/latest/Appendices/Appendix11.html): Boolean results and the SuperBASIC operator order. The order is a compatibility reference; QL/32 groups comparisons and uses a smaller operator set.
- [TO](https://superbasic-manual.readthedocs.io/en/latest/T/to.html) and [WIDTH](https://superbasic-manual.readthedocs.io/en/latest/W/width.html): cursor-column and device-width effects relevant to PRINT separators.
- [The New User Guide, PRINT](https://sinclairql.net/djw/mags/qluserworld/QL%20New%20User%20Guide.pdf): semicolon, comma, exclamation, backslash, and output formatting behavior (PDF, section 23).
- [Original QL User Guide](https://sinclairql.speccy.org/archivo/docs/sys/QL_UsersGuide.pdf): `EDIT` line entry and replacement behavior; the [online EDIT reference](https://superbasic-manual.readthedocs.io/en/latest/E/edit.html) documents replacement of an existing line number.

## Psion OPL

- [Leigh Edwards, *Programming Psion Computers*](https://palmtop.cosi.com.pl/wp-content/uploads/2013/07/Programming-Psion-Computers.pdf). OPL Reference chapter 9: menus on PDF viewer pages 280–284 and dialogs on pages 286–298.
- Its `dINIT`/`DIALOG` controls and `mINIT`/`mCARD`/`MENU` illustrate the UI model. OPL variants differ by device. QL/32 adopts the model with its own syntax and session ownership.

The external manuals remain reference material; do not copy their text, examples, or PDF files into this public repository without redistribution rights.

