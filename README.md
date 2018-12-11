# `wasm` and `wlink`

`wasm` and `wlink` are, respectively, the assembler and linker for WRAMP code.
Assembly code, usually in a .s file, is given to `wasm`, which outputs an object file.
One or more object files can be linked together into a program by `wlink`, outputting
an .srec file.
These .srec files are then passed directly to WRAMPmon via `remote`, or further processed
by `trim` to create a .mem file which can be used as part of Vivado's synthesis tool
when loading new versions of WRAMPmon onto physical Basys3 boards.

## Usage

`wasm` takes a single input file and produces a single output file, which defaults to
the name of the input file with a .o file extension. Alternatively, an output file can
be chosen using the `-o` argument.

` $ wasm -o output.o input.s `

`wlink` takes an arbitrary number of input files, and produces a single output file, which
defaults to link.out in the current working directory. 
Again, an output file can be chosen.

` $ wlink -o output.srec input1.o input2.o input3.o `

## Building

Building `wasm` and `wlink` simply requires `g++` to be installed.
Type `make`, or specify a single program with `make wasm` or `make wlink`.