# Language reference sources

These are the sources linked in the “QL32 Project Idea” chat and used to compile [QL32-LANG-001](../specification/QL32-LANG-001.md). The manuals are linked rather than redistributed. *Programming Psion Computers* explicitly reserves reproduction rights.

## SuperBASIC / SMSQ/E

- [SBASIC/SuperBASIC Reference Manual Online](https://superbasic-manual.readthedocs.io/en/latest/) (online edition 4.0.2). It includes QL ROM, SMSQ/E, and optional toolkit commands. Check each command's **Location** and **SMS NOTES** before claiming SMSQ/E compatibility.
- [Original Sinclair QL User Guide](https://sinclairql.speccy.org/archivo/docs/sys/QL_UsersGuide.pdf), “keyword” section in the Concepts guide: mixed-case keyword notation marks the minimum accepted abbreviation; entered keywords are reproduced in full. It gives `REPeat` → `REP` as an example.
- [IF](https://superbasic-manual.readthedocs.io/en/latest/I/if.html): inline and block syntax.
- [FOR](https://superbasic-manual.readthedocs.io/en/latest/F/for.html): inline and block forms, mixed lists of single values and stepped intervals, `NEXT`, and `EXIT`.
- [DEFine PROCedure](https://superbasic-manual.readthedocs.io/en/latest/D/define--procedure.html), [END DEFine](https://superbasic-manual.readthedocs.io/en/latest/E/end--define.html), and [LOCal](https://superbasic-manual.readthedocs.io/en/latest/L/local.html): calls, returns, local variables.
- [DIV](https://superbasic-manual.readthedocs.io/en/latest/D/div.html): floor quotient and paired `MOD` behavior.
- [DIM](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html): numeric arrays have zero-based, inclusive upper bounds; historical string arrays have additional fixed-length semantics.

## Psion OPL

- [Leigh Edwards, *Programming Psion Computers*](https://palmtop.cosi.com.pl/wp-content/uploads/2013/07/Programming-Psion-Computers.pdf). OPL Reference chapter 9: menus on PDF viewer pages 280–284 and dialogs on pages 286–298.
- Its `dINIT`/`DIALOG` controls and `mINIT`/`mCARD`/`MENU` illustrate the UI model. OPL variants differ by device. QL/32 adopts the model with its own syntax and session ownership.

The external manuals remain reference material; do not copy their text, examples, or PDF files into this public repository without redistribution rights.

