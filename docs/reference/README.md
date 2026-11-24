# Language reference sources

The implementation-facing language manual is
[QL/32 BASIC Language Reference](QL32-Language-Reference.md). It summarizes
implemented syntax, built-ins, target support, and hardware drivers. This page
records external source material and compatibility references.

These are the sources linked in the “QL32 Project Idea” chat and used to compile [QL32-LANG-001](../specification/QL32-LANG-001.md). The manuals are linked rather than redistributed. *Programming Psion Computers* explicitly reserves reproduction rights.

## SuperBASIC / SMSQ/E

- [SBASIC/SuperBASIC Reference Manual Online](https://superbasic-manual.readthedocs.io/en/latest/) (online edition 4.0.2). It includes QL ROM, SMSQ/E, and optional toolkit commands. Check each command's **Location** and **SMS NOTES** before claiming SMSQ/E compatibility.
- [Toolkit II manual](https://dilwyn.theqlforum.com/docs/ebooks/tk2_a4.pdf): Toolkit II is treated as a widely adopted, near-standard SuperBASIC extension. It supplied substantial facilities planned for the QL language/system but missing from the rushed initial release. QL/32 may use it as a compatibility and design reference; identify toolkit-derived behavior as such and check implementation notes rather than implying it shipped in the original ROM.
- [Original Sinclair QL User Guide](https://sinclairql.speccy.org/archivo/docs/sys/QL_UsersGuide.pdf), “keyword” section in the Concepts guide: mixed-case keyword notation marks the minimum accepted abbreviation; entered keywords are reproduced in full. It gives `REPeat` → `REP` as an example.
- [IF](https://superbasic-manual.readthedocs.io/en/latest/I/if.html): inline and block syntax; conditions and Boolean results use true/false and numeric `1`/`0`.
- [FOR](https://superbasic-manual.readthedocs.io/en/latest/F/for.html) and [END FOR](https://superbasic-manual.readthedocs.io/en/latest/E/end--for.html): inline and block forms, mixed lists of single values and stepped intervals, `NEXT`, and `EXIT`.
- [REPeat](https://superbasic-manual.readthedocs.io/en/latest/R/repeat.html), [END REPeat](https://superbasic-manual.readthedocs.io/en/latest/E/end--repeat.html), [NEXT](https://superbasic-manual.readthedocs.io/en/latest/N/next.html), and [EXIT](https://superbasic-manual.readthedocs.io/en/latest/E/exit.html): named loop syntax and loop-control behavior.
- [GO TO](https://superbasic-manual.readthedocs.io/en/latest/G/go--to.html): QL ROM line-number jump syntax. QL/32 tests numeric targets; named labels are a QL/32 extension.
- [END SELect](https://superbasic-manual.readthedocs.io/en/working/E/end--select.html) and [The Sinclair QL Series: QL SuperBASIC and its Applications](https://trastero.speccy.org/cosas/Libros/QL/PDF/The_Sinclair_QL_Series_QL_SuperBASIC_and_its_Applications_%5BEN%5D.pdf): `SELect ON`, `= value`, `= start TO end`, `= REMAINDER`, and block termination. The implementation supports the block form; its exact singleton matching remains QL/32-specific pending tests against SBASIC.
- [DEFine PROCedure](https://superbasic-manual.readthedocs.io/en/latest/D/define--procedure.html), [END DEFine](https://superbasic-manual.readthedocs.io/en/latest/E/end--define.html), and [LOCal](https://superbasic-manual.readthedocs.io/en/latest/L/local.html): calls, returns, local variables.
- [DIV](https://superbasic-manual.readthedocs.io/en/latest/D/div.html): floor quotient and paired `MOD` behavior.
- [DIM](https://superbasic-manual.readthedocs.io/en/latest/D/dim.html): numeric arrays have zero-based, inclusive upper bounds; historical string arrays have additional fixed-length semantics.
- [A11 Mathematics](https://superbasic-manual.readthedocs.io/en/latest/Appendices/Appendix11.html): Boolean results and the SuperBASIC operator order. The order is a compatibility reference; QL/32 groups comparisons and uses a smaller operator set.
- [TO](https://superbasic-manual.readthedocs.io/en/latest/T/to.html) and [WIDTH](https://superbasic-manual.readthedocs.io/en/latest/W/width.html): cursor-column and device-width effects relevant to PRINT separators.
- [The New User Guide, PRINT](https://sinclairql.net/djw/mags/qluserworld/QL%20New%20User%20Guide.pdf): semicolon, comma, exclamation, backslash, and output formatting behavior (PDF, section 23).
- [Original QL User Guide](https://sinclairql.speccy.org/archivo/docs/sys/QL_UsersGuide.pdf): `EDIT` line entry and replacement behavior; the [online EDIT reference](https://superbasic-manual.readthedocs.io/en/latest/E/edit.html) documents replacement of an existing line number.

## Psion OPL

- [Psion Series 3c, Series 5 and Siena OPL User Guide](PSION%203c%20Series5%20y%20Siena%20Programming%20Ref.pdf), supplied in `docs/reference`. Its Basics and Graphics & Friendlier Interaction sections cover OPL declarations, control flow, pixel windows, menus, and dialogs. OPL behavior varies by device generation; observe the Series 5/Series 3c markers.
- The Series 5 OPL32 `INCLUDE` reference (Basics, “Series 5 Header Files, Constants and Procedure Prototypes”; OPL Reference chapter 9) places includes at module scope and describes header constants/prototypes rather than procedure bodies. QL/32's source-file inclusion is a documented adaptation, not OPL32 compatibility.
- [Leigh Edwards, *Programming Psion Computers*](Programming-Psion-Computers.pdf), supplied in `docs/reference`; see OPL Reference chapter 9 for menus and dialogs, and the application architecture discussions for broader UI context.
- [OPL `gUPDATE` reference](https://www.unix-ag.uni-siegen.de/docs/psionopl/b06graph.htm) describes disabling automatic redraw during a graphics batch and forcing a refresh with `gUPDATE`. QL/32 adopts that workflow; its `AUTO` mode is a QL/32 extension.
- OPL uses floating-point reals by default, with `%` and `&` suffixes for 16-bit and 32-bit integers. It has no per-program single/double precision switch. The [OPL values reference](https://waghornswood.net.nz/Auda/psion/sysdoc/opl/lang/basics.html) documents the real range and typed integers; the local OPL32 runtime represents real variables as `TReal64`.
- Its `dINIT`/`DIALOG` controls and `mINIT`/`mCARD`/`MENU` illustrate the UI model. OPL variants differ by device. QL/32 adopts the model with its own syntax and session ownership.

## Numeric precision and platform options

- Original SuperBASIC uses its built-in floating-point format by default (eight significant decimal digits in the QL manual) and `%` for 16-bit integer variables; it does not select single or double precision per program. See the [SuperBASIC concept guide](https://sinclairql.net/djw/docs/ebooks/olqlug/QL%20Manual%20-%20Concepts.htm).
- Annex32 stores numeric variables as double-precision floating point and offers `OPTION.CPUFREQ 80|160|240` for device configuration; it has no single/double math mode. QL/32 adopts those CPU frequency choices with named constants. See the [Annex32 language and options reference](https://www.cicciocb.com/annex32help/V1.518/start.htm).
- QL/32 defines `OPTION KOptMathSingle` / `KOptMathDouble`, defaulting to DOUBLE, and ESP32-specific `OPTION KEspCpuFreq80` / `160` / `240`. Host builds accept the CPU option and ignore it.

## Annex32 Wi-Fi BASIC

- [Annex32 Wi-Fi BASIC help](https://cicciocb.com/annex32help/V1.43/start.htm) documents `LEFT$`, `RIGHT$`, `MID$`, including the optional length argument to `MID$`, and 32-bit bitwise operators, shifts, and hexadecimal values. Annex32 is a separate ESP32-oriented BASIC dialect and is used only as a design reference.

The OPL manuals are design references for managed UI and selected language
refinements, not a source-compatibility target. SuperBASIC remains the core
language. The QL/32 M3 graphics contract selectively uses OPL's pixel-based
window model; see [QL32-UI-001](../specification/QL32-UI-001.md). The manuals
are copyrighted and must not be reproduced in project documentation.

The external manuals remain reference material; do not copy their text, examples, or PDF files into this public repository without redistribution rights.

## Hardware platform

- [LILYGO VGA32 V1.4 product page](https://lilygo.cc/en-de/products/fabgl-vga32) for target module details.
- [LILYGO VGA32/FabGL GPIO assignment](https://github.com/Xinyuan-LilyGO/VGA32/blob/master/fabgl%20GPIOs%20assignment.txt) for VGA, PS/2, audio, and SD pin mapping.
- [FabGL v1.0.9](https://github.com/fdivitto/FabGL/releases/tag/v1.0.9) is the pinned graphics/PS/2 dependency. Its upstream project states the Arduino-ESP32 compatibility ceiling and GPL-3.0 license; see `boards/lilygo_vga32/README.md` before redistributing firmware.
- [ESP32-2432S028R product specification](https://make.net.za/wp-content/datasheets/Shenzhen%20Jingcai%20Intelligent%20ESP32-2432S028R%20Product%20Spec.pdf) identifies the 2.8-inch ILI9341 panel and 320x240 resolution. [CYD pin reference](https://esp3d.io/esp3d-tft/version_1x/hardware/esp32/sunton-28-2432/) records the commonly used TFT and SD GPIO mapping; check board revisions before using it.
- [Adafruit ILI9341 library](https://github.com/adafruit/Adafruit_ILI9341) and its [SPIClass constructor](https://github.com/adafruit/Adafruit_ILI9341/blob/master/Adafruit_ILI9341.h) are used by the CYD TFT adapter.

