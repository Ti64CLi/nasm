# nasm (v1.1)

An ARM assembler for the TI-Nspire CX / CX II, running natively on-calc or on a computer.

This is a C port of the original Python `nAssembler` by **lkj**. Moving from Micropython to native C (via Ndless) makes assembly much faster and adds a built-in file browser and error-reporting UI for on-calc use.


## Features

* **Native Execution:** Runs directly on the TI-Nspire via Ndless, no Micropython interpreter required.
* **Built-in GUI:** A graphical file browser to navigate your `/documents` directory and select source files. The browser remembers the last directory you used.
* **Output:** The output executable is written in the same directory as the source (e.g., compiling `test.asm.tns` outputs `test.tns`).
* **Scrollable Error UI:** If assembly fails, a scrollable text window displays exactly which file and line caused each error (including through `INCLUDE`).
* **Settings:** Press the **Cat** (catalog) key in the file browser to open the preferences window (theme, source-file extension, ...). Settings are shared with nStudio via `/documents/ndless/nstudio.cfg`.
* **Verified encodings:** a regression suite byte-compares the assembler's output against the GNU assembler from the Ndless toolchain (`nspire-as`) (see **Testing** below).


## Usage

1. **Prerequisites:** [Ndless](https://ndless.me) must be installed on your TI-Nspire CX or CX II.
2. **Launch:** Open the `nasm.tns` executable on your calculator.
3. **Select Source:** The built-in file browser will open.
    * Use the **Up/Down** arrows to navigate.
    * Press **Tab** to toggle the file filter (between `*.asm.tns` and all files).
    * Press **Cat** to open the settings window.
    * Press **Enter** to select your input file containing the assembly source code.
4. **Assemble:** The program will automatically assemble the file.
    * If successful, an alert window shows the output path and code size, then returns to the browser for the next assemble/fix cycle.
    * If there are errors, a scrollable window lists them so you can debug your code.
5. **Exit:** Press **Esc** in the file browser.


## Building from Source

To compile this project yourself, you will need the Ndless SDK installed on your computer. Then run:

```bash
make
```

This uses the provided `Makefile` to compile the C files and generate the `nasm.tns` executable using `nspire-gcc` and `genzehn`.


## Testing

The `tests/` directory contains a regression suite that compiles the assembler core for the build machine and **byte-compares** its output against `nspire-as` (the GNU assembler of the Ndless toolchain) for every supported instruction form, plus a set of must-fail sources. Run it with:

```bash
make test
```

It requires `gcc` and the Ndless SDK (either `nspire-as` in `PATH` or `NDLESS_SDK` pointing at the SDK root).


## Assembly Language Syntax

*(Based on the original documentation by lkj)*

* **Indentation:** Instruction names must be preceded by whitespace.
* **Labels:** Labels must *not* be preceded by any whitespace.
* **Standard ARM:** The instruction names and syntax are like standard ARM assembly (matching official ARM online documentation). Both pre-UAL (`LDREQB`, `LDMEQFD`) and UAL (`LDRBEQ`, `LDMFDEQ`, `SUBSEQ`) suffix orders are accepted.
* **Explicit Operands:** Two operands where three are required is not allowed (no implicit destination register). For example, `ADD r0,#28` must be written out fully as `ADD r0,r0,#28`.
* **No Auto-substitution:** There is no automatic substitution of instructions (e.g., swapping `MVN` for `MOV` if the immediate value can only be encoded in the inverted case). The one exception is `LDR rX,=value`, which assembles to `MOV`/`MVN` when the constant is encodable.

### Supported Instructions

All usual instructions are supported:
`ADC(S)`, `ADD(S)`, `AND(S)`, `B`, `BIC(S)`, `BL`, `BX`, `CLZ`, `CMP`, `CMN`, `EOR(S)`, `LDM..`, `LDR(B/T/BT/H/SH/SB)`, `MCR`, `MLA(S)`, `MOV(S)`, `MRC`, `MRS`, `MSR`, `MUL(S)`, `MVN(S)`, `ORR(S)`, `RSB(S)`, `RSC(S)`, `SBC(S)`, `SMLAL(S)`, `SMULL(S)`, `STM..`, `STR(B/T/BT/H)`, `SUB(S)`, `SVC`/`SWI`, `SWP(B)`, `TEQ`, `TST`, `UMLAL(S)`, `UMULL(S)`

### Pseudo-instructions

* **`ADR rX, label`** : loads the address of a nearby label with a PC-relative `ADD`/`SUB`.
* **`PUSH {list}` / `POP {list}`** : aliases for `STMFD SP!,{list}` / `LDMFD SP!,{list}`. A single-register list uses the `STR`/`LDR` encoding, as standard assemblers do.
* **`NOP`** : assembles to `MOV R0,R0`.
* **`LDR rX, =value`** : loads an arbitrary 32-bit constant. If the constant fits an immediate it becomes `MOV`/`MVN`; otherwise it is placed in a *literal pool* and loaded PC-relative. Pools are emitted at each `LTORG` directive and automatically at the end of the program. `value` may also be a named constant or label expression (evaluated as an offset from the start of the program).

### Directives

The following directives are supported:
`ALIGN`, `DCD`, `DCDU`, `DCW`, `DCWU`, `DCB`, `EQU`, `INCBIN`, `INCLUDE`/`GET`, and `LTORG`.

* **`name EQU value`** : defines a named constant (the label goes in column 0, like any label). The value is a numeric literal or a previously defined symbol ± offset.
* Named constants and label expressions can be used **wherever an immediate is allowed**: `MOV r0,#WIDTH`, `LDR r1,[r2,#OFFSET]`, `MOV r3,r4,LSL #SHIFT`, `DCD start`, `DCB COUNT-1`, `SWI SWINUM`, ... (address labels evaluate to their offset from the start of the program).

### Numeric Literals

* **Hexadecimal:** Prefix with `0x`
* **Octal:** Prefix with `0`
* **Binary:** Prefix with `0b`
* **ASCII:** Single characters enclosed in single quotes `'` are interpreted as their ASCII value.
* **Strings:** You can use `DCB "some string"` to create a string in memory (commas and semicolons inside quotes are handled).

### Entry Point

The first line of the source is also the entry point of the program.


## Examples

The `examples/` directory contains ready-to-assemble sources:

* `clrscr.asm.tns` : clears the screen and waits (from the original nAssembler).
* `features.asm.tns` : demonstrates `EQU`, `PUSH`/`POP`, `NOP`, `LDR =` literal pools and post-indexed addressing by filling the screen in two colours.


## Credits & Original Project

This project is a C port of the original Python `nAssembler` created by **lkj**.

* **Original Repo:** [nAssembler by lkjcalc](https://github.com/lkjcalc/nAssembler/)
* **Original Author Email:** <lkjcalc@gmail.com>

## License

This software is licensed under the **GNU GPL v3**, carrying forward the original license chosen by lkj. See the `LICENSE` file for full details.
