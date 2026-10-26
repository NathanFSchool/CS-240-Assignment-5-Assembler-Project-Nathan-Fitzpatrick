# CS 240 Assignment 5: Assembler and Disassembler

**Author:** Nathan Fitzpatrick

This project contains two C++ programs that convert a MIPS FizzBuzz program between assembly source and machine code.

The assembler reads the source one line at a time using file input streams. It uses two passes to resolve labels and generate machine code. The disassembler reads either machine-code format and reconstructs a MIPS assembly file.

## Requirements

- Windows with CLion and a configured C++ compiler.
- CMake 4.3 or newer, as specified in the project files.
- C++20, as configured in both projects.
- MARS to run the original and reconstructed MIPS programs.

## Project Files

| File or folder | Purpose |
|---|---|
| `Assembler/main.cpp` | Assembler source code |
| `Assembler/CMakeLists.txt` | Assembler build configuration |
| `disassembler/disassembler.cpp` | Disassembler source code |
| `disassembler/CMakeLists.txt` | Disassembler build configuration |
| `mips5FIzzBuzz.asm` | Original MIPS FizzBuzz source |
| `fizzbuzz.mhc-ascii` | Machine code stored as readable binary digits |
| `fizzbuzz.mhc-bin` | Machine code stored as raw bytes |

**Important:** The disassembler also names its output `mips5FIzzBuzz.asm`. Save a backup of the original source before running it, since an existing file with that name in the working directory will be replaced.

## Running the Assembler in CLion

1. Download or clone this repository.
2. Open the `Assembler` folder as a project in CLion and allow CMake to load.
3. Open **Run → Edit Configurations** and select the `Assembler` configuration.
4. Set **Working directory** to the repository’s top-level folder containing `mips5FIzzBuzz.asm`.
5. Set **Program arguments** to `mips5FIzzBuzz.asm`.
6. Build and run the program.

The assembler creates `fizzbuzz.mhc-ascii` and `fizzbuzz.mhc-bin` in the working directory. It also displays each instruction alongside its binary encoding in the console.

## Running the Disassembler in CLion

1. Open the `disassembler` folder as a separate CLion project.
2. Open **Run → Edit Configurations** and select the `disassembler` configuration.
3. Set **Working directory** to the repository’s top-level folder.
4. Leave **Program arguments** blank, then build and run.
5. At the filename prompt, enter either `fizzbuzz.mhc-ascii` or `fizzbuzz.mhc-bin`.

Alternatively, enter either filename in **Program arguments** to skip the prompt.

The program identifies the format from its extension and creates `mips5FIzzBuzz.asm` in the working directory. Only one input file is needed per run.

## Supported Features and File Format

The assembler supports the instructions needed by this FizzBuzz program, forward and backward labels, and the `li`, `la`, `move`, and `nop` pseudoinstructions. Supported directives include `.text`, `.data`, `.globl`, `.asciiz`, `.ascii`, and `.byte`.

Both machine-code formats contain a 32-byte MHC1 header followed by instruction bytes and data bytes. The header preserves section addresses, section sizes, and the program’s entry address.

- Instructions begin at `0x00400000`.
- Data begins at `0x10010000`.
- Header fields and instruction words use big-endian byte order.
- Each instruction uses 32 binary digits in the ASCII format or four bytes in the binary format, excluding header and formatting overhead.

The disassembler generates new labels and preserves data using `.byte` directives. Original comments and pseudoinstruction spellings are not recovered.

## Testing and Results

The following tests were completed successfully:

| Test | Result |
|---|---|
| Assemble the original MIPS source | Both output files generated successfully |
| Disassemble `fizzbuzz.mhc-ascii` | Valid MIPS assembly generated |
| Disassemble `fizzbuzz.mhc-bin` | Valid MIPS assembly generated |
| Run the reconstructed program in MARS | Output matches the original program |
| Reassemble the reconstructed source | Machine-code bytes match the original |
| Supply invalid filenames or invalid input | Useful error messages displayed |

The supplied MIPS program checks the hard-coded value `9` and prints `Fizz`. Run the original and reconstructed assembly in MARS with delayed branching disabled and the default memory layout.

To repeat the byte-comparison test, save copies of both machine-code files before reassembling the reconstructed source. Compare the newly generated files against those saved copies.

## Sources and Collaboration

[Add your sources and collaboration acknowledgement here.]
