# nasm (v1.0)

An ARM Assembler for the TI-Nspire CX / CX II, running natively on-calc or on a computer.

This is a high-performance C translation of the original Python-based `nAssembler` by **lkj**. By moving from Micropython to native C (via Ndless), this version drastically improves assembly speed and introduces a built-in graphical file browser and error-reporting UI for a seamless on-calc development experience.


## Features

* **Native Execution:** Runs directly on the TI-Nspire via Ndless—no Micropython interpreter required.
* **Built-in GUI:** Features a clean, graphical file browser to navigate your `/documents` directory and select source files.
* **Smart Output:** Automatically generates the output executable in the same directory (e.g., compiling `test.asm.tns` outputs `test.tns`).
* **Scrollable Error UI:** If assembly fails, a scrollable text window displays exactly which lines caused the syntax errors.


## Usage

1. **Prerequisites:** Ensure you have [Ndless](https://ndless.me) installed on your TI-Nspire CX or CX II.
2. **Launch:** Open the `nasm.tns` executable on your calculator.
3. **Select Source:** The built-in file browser will open.
    * Use the **Up/Down** arrows to navigate.
    * Press **Tab** to toggle the file filter (between `*.asm.tns` and all files).
    * Press **Enter** to select your input file containing the assembly source code.
4. **Assemble:** The program will automatically assemble the file.
    * If successful, an alert window will confirm the output path. You can find your compiled `.tns` executable in the doc browser (you may need to go to the homescreen and back to refresh it).
    * If there are errors, a scrollable window will list them so you can debug your code.


## Building from Source

To compile this project yourself, you will need the Ndless SDK installed on your computer.

Simply run:

```bash
make
```

This uses the provided `Makefile` to compile the C files and generate the `nasm.tns` executable using `nspire-gcc` and `genzehn`.


## Assembly Language Syntax

*(Based on the original documentation by lkj)*

**IMPORTANT (syntax is not very flexible at the moment):**

* **Indentation:** Instruction names must be preceded by whitespace.
* **Labels:** Labels must *not* be preceded by any whitespace.
* **Standard ARM:** The instruction names and syntax are like standard ARM assembly (matching official ARM online documentation).
* **Explicit Operands:** Two operands where three are required is not allowed (no implicit destination register). For example, `ADD r0,#28` must be written out fully as `ADD r0,r0,#28`.
* **No Auto-substitution:** There is no automatic substitution of instructions (e.g., swapping `MVN` for `MOV` if the immediate value can only be encoded in the inverted case).

### Supported Instructions

All usual instructions are supported:
`ADC(S)`, `ADD(S)`, `AND(S)`, `B`, `BIC(S)`, `BL`, `BX`, `CLZ`, `CMP`, `CMN`, `EOR(S)`, `LDM..`, `LDR(B/T/BT/H/SH/SB)`, `MCR`, `MLA(S)`, `MOV(S)`, `MRC`, `MRS`, `MSR`, `MUL(S)`, `MVN(S)`, `ORR(S)`, `RSB(S)`, `RSC(S)`, `SBC(S)`, `SMLAL(S)`, `SMULL(S)`, `STM..`, `STR(B/T/BT/H)`, `SUB(S)`, `SVC`/`SWI`, `SWP(B)`, `TEQ`, `TST`, `UMLAL(S)`, `UMULL(S)`

*Note: Some pseudo-instructions (like `PUSH` etc.) are not implemented. The `ADR` pseudo-instruction is implemented.*

### Directives

The following directives are supported:
`ALIGN`, `DCD`, `DCDU`, `DCW`, `DCWU`, `DCB`, `INCBIN`, and `INCLUDE`.

### Numeric Literals

* **Hexadecimal:** Prefix with `0x`
* **Octal:** Prefix with `0`
* **Binary:** Prefix with `0b`
* **ASCII:** Single characters enclosed in single quotes `'` are interpreted as their ASCII value.
* **Strings:** You can use `DCB "some string"` to create a string in memory.

### Entry Point

The first line of the source is also the entry point of the program.


## Credits & Original Project

This project is a C port of the original Python `nAssembler` created by **lkj**.

* **Original Repo:** [nAssembler by lkjcalc](https://github.com/lkjcalc/nAssembler/)
* **Original Author Email:** <lkjcalc@gmail.com>

## License

This software is licensed under the **GNU GPL v3**, carrying forward the original license chosen by lkj. See the `LICENSE` file for full details.
