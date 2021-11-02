# myshell

## Overview
`MyShell` is a bash-like shell implemented in Linux C, which is well-structured, efficient and memory-saving, and supports the following features.

## Features
- Support built-in Linux commands.
- Support redirection and pipelining.
- Support quotations in input.
- Support waiting incomplete command.
- Support background running.
- Support CTRL-C and CTRL-D.
- Support error handling.
- Self-implemented built-in command `pwd` and `cd`.

## Compile & Run
In the project directory, type:
```
mkdir build
cd build
cmake ..
make
mv myshell myshell_memory_check ..
cd ..
```
For later compiling, you do not need to type `mkdir build` command.

Now you have two executables in the project directory:
- `myshell`: The main executable.
- `myshell_memory_check`: The executable featuring memory checking. If you alter the source code, this executable can help you debug.

To run, type:
```
./myshell
```
or:
```
./myshell_memory_check
```
