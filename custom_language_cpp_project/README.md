# Plainspeak compiler and interpreter (C++)

Plainspeak is a small, English-like programming language. This project checks a
Plainspeak program (`.psk` file) completely at compile time, then runs it in a
tree-walking interpreter. It can also print the program's tokens, a
three-address intermediate representation, or optimised LLVM IR.

- **Language reference:** the *Plainspeak User Guide* (LMTE Assignment 2b) is the
  specification.
- **How each compiler stage works:** [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md).

## Contents

1. [Quick start](#1-quick-start)
2. [Building](#2-building)
3. [Running a program](#3-running-a-program)
4. [Testing](#4-testing)
5. [Language overview](#5-language-overview)
6. [Scope rules](#6-scope-rules)
7. [Errors and exit codes](#7-errors-and-exit-codes)
8. [Project structure](#8-project-structure)

## 1. Quick start

```bash
cd custom_language_cpp_project
make                       # builds ./customlang
./customlang demo.psk      # runs the demo (asks for your name and a number)
./test.sh                  # runs the test suite
```

## 2. Building

Requirements:

- a C++17 compiler (`clang++` or `g++`)
- `make`, or CMake 3.16+
- *optional:* the LLVM development libraries (`llvm-config`), needed only for the
  `--llvm-ir` option (see [Installing LLVM](#installing-llvm-optional))

```bash
make clean && make
```

The Makefile looks for `llvm-config` on your `PATH` and through
`brew --prefix llvm`:

- **LLVM found:** the full compiler is built.
- **LLVM not found:** you'll see the note
  `LLVM not found - building without --llvm-ir support`. Everything works except
  `--llvm-ir`, which then reports that it is unavailable.

Other build options:

| Command | Effect |
|---|---|
| `LLVM_CONFIG=/path/to/llvm-config make` | Use a specific LLVM install. |
| `make NO_LLVM=1` | Build without LLVM even if it is installed. |
| `make run` | Build, then run `demo.psk`. |
| `make test` | Build, then run `./test.sh`. |

After installing LLVM, or switching between LLVM and non-LLVM builds, run
`make clean`. Header dependencies are tracked automatically, so editing a `.hpp`
file rebuilds every source file that includes it.

With CMake:

```bash
cmake -S . -B build
cmake --build build
./build/customlang demo.psk
```

Build outputs (`*.o`, `*.d`, `customlang`, `build/`) are not committed.
Always build the compiler on your own machine.

### Installing LLVM (optional)

LLVM is only needed for `--llvm-ir`. Install it, check that `llvm-config --version`
prints a version, then run `make clean && make`.

**macOS (Homebrew)**

```bash
brew install llvm
```

The Makefile finds Homebrew's LLVM automatically, even though it is not on your `PATH`.

**Linux**

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install build-essential llvm llvm-dev

# Fedora
sudo dnf install gcc-c++ make llvm-devel

# Arch
sudo pacman -S base-devel llvm
```

Some distributions only install a versioned name such as `llvm-config-18`. Point
the Makefile at it with the full path:

```bash
LLVM_CONFIG=/usr/bin/llvm-config-18 make
```

If linking fails with a missing `-lz`, `-lzstd`, `-ltinfo` or `-lxml2`, install the
matching development package (for example
`sudo apt install zlib1g-dev libzstd-dev libtinfo-dev libxml2-dev`).

**Windows**

Use [MSYS2](https://www.msys2.org/). It provides `g++`, `make`, `bash` and an LLVM
build that includes `llvm-config`. (The LLVM installer from llvm.org ships only the
tools, not `llvm-config` and the development libraries.)

1. Install MSYS2 from <https://www.msys2.org/>.
2. Open the **MSYS2 UCRT64** terminal from the Start menu and run:

   ```bash
   pacman -Syu
   pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-llvm
   ```

3. In the same UCRT64 terminal, build and test as usual:

   ```bash
   cd /c/path/to/custom_language_cpp_project
   llvm-config --version
   make clean && make
   ./customlang demo.psk
   ./test.sh
   ```

To build on Windows without LLVM, install only `make` and `mingw-w64-ucrt-x86_64-gcc`.
The Makefile then builds without `--llvm-ir` support. On Windows the program is
built as `customlang.exe` with a 16 MB stack.

### What LLVM is used for

In this project as it currently works, LLVM is mainly useful for generating and
inspecting optimized LLVM IR with `--llvm-ir`. It demonstrates how the supported
part of Plainspeak could be translated for LLVM, and provides a starting point for
a future native-code backend.

It does not currently make your program run faster or create an executable: after
printing the LLVM IR, `customlang` still runs the program using its interpreter.

## 3. Running a program

```bash
./customlang <file.psk> [--tokens] [--ir] [--llvm-ir]
```

| Option | Effect |
|---|---|
| *(none)* | Check the program, then run it. |
| `--tokens` | Print every token with its `line:column`, then run. |
| `--ir` | Print the optimised three-address IR, then run. |
| `--llvm-ir` | Print LLVM IR after LLVM's O2 pipeline, then run. Supports only scalar variables, arithmetic, comparisons, `say`, `if` and loops. |

The program is tokenised, parsed, semantically checked and optimised first. If
any check fails, nothing runs.

Programs that use `ask` read from the keyboard. Input can also be piped in:

```bash
printf '23\n1.75\nAda Lovelace\ntrue\n' | ./customlang tests/input.psk
```

## 4. Testing

```bash
./test.sh
```

The script rebuilds from scratch, then runs `demo.psk`, the command-line checks
and every program in `tests/`. The checks are grouped by feature (lexing and
syntax, types and strings, input, control flow and functions, scope, classes,
optimiser, LLVM). Each program is checked in one of three ways:

- **Positive:** it must print an exact output.
- **Negative:** it must be rejected with the expected exit code (1 = compile
  error, 2 = run-time error) and error message.
- **Inspection:** its `--tokens`, `--ir` or `--llvm-ir` output must contain what's expected.

The script stops at the first failure and prints the expected and actual output.
It also fails if a program in `tests/` isn't used by any check. On success it
prints `PASS: all N checks`. If the build has no LLVM, the LLVM checks print
`SKIP` instead of failing. The last check (the infinite-loop guard) takes a few
seconds.

To check your own program, run `./customlang yourfile.psk`. To check that a
mistake is caught, look for an `ERROR:` line and exit code 1 (`echo $?`).

## 5. Language overview

The User Guide is the full reference. This section is a quick reminder.

```text
// Comments start with //
initialize int a;
set 5 to a                    // declare and assign (int, integer, float, string, bool)
initialize string name            // declare with a default value
set score to score + 5            // assign (also: score = score + 5)

fix limit                         // make a variable constant

say "Hello " + name               // print (also: cout << x)
ask "Your name? " into name       // read a line into a declared variable

add 5 to score                    // also: subtract 5 from x, multiply 2 to x,
divide score by 2                 //       divide x by 2, mod x by 3

if score is greater than 10 {     // English comparison (only one per condition)
    say "big"
} otherwise {
    if (score == 10) { say "ten" } otherwise { say "small" }   // no 'else if': nest instead
}
if (score > 1 and score < 99) { say "in range" }   // combine tests with symbols

check score {                     // switch: first match wins, no fall-through
    when 1 { say "one" }
    otherwise { say "other" }
}

while (score < 20) { add 1 to score }
repeat 3 times { say "hi" }
for (int i = 0; i < 3; i++) { say i } // fast initialise for for loop
for (i = 0; i < 3; i++) { say i }  // reuse the counter in a later loop

initialize list values             // lists grow, hold mixed types, index from 0
put 10 on values
say item of values at 0
say length of values
say top of values
for each v in values { say v }     
stop /skip.    // break / continue work in every loop
remove item of values

define function double(integer n) returning integer {
    return n * 2
}
say double(4)

define function identity<T>(T value) returning T { return value }   // generics

define struct Point { integer x  integer y }
define enum Colour {red, green, blue}

abstract define class Animal {
    public initialize string name
    public abstract define function speak() returning nothing
}
define class Dog inherits Animal {
    public define constructor {string who} { set name to who }
    public define function speak() returning nothing { say "Woof!" }
}
Initialize Dog pike with ("Pike")
call speak() from pike
say name of pike
```

Rules worth remembering:

- **Strict strings.** A string variable can only be given a quoted literal or another string variable. `string s = "a" + "b"` and `string s = name()` are rejected. Joined strings can still be printed.
- **Strict types.** Types must match. An int may go into a float, never the reverse; this also applies to `add`/`subtract`/... commands. Conditions must be `true`/`false` values.
- **Object variables.** An object variable can only hold an object of its own class or a subclass.
- **Constructors.**
  - A class with no constructor uses its parent's.
  - A constructor can call the parent constructor with `super(...)` as its first statement.
  - Without `super`, the parent's no-argument constructor runs automatically first.
  - If the parent constructor needs arguments, `super(...)` is required.
- **Methods.** Methods can use the object's fields, including list fields (`put 1 on items`), but can't call another method of the same object.
- **Reserved words.** Keywords can't be used as names. These are:
  - types and values: `int`, `integer`, `float`, `string`, `bool` `list`, `nothing`, `true`, `false`
  - operators: `and`, `or`, `not`, `mod`, `is`, `of`
  - every statement keyword (`say`, `set`, `if`, `while`, `define`, …)
  - list words: `item`, `length`, `top`
  - class words: `public`, `private`, `inherits`, `constructor`, `returning`
- **List indexes.** `item of l at i + 1` uses `i + 1` as the index. A comparison after it applies to the item: `item of l at 0 > 5` compares the item with 5.

## 6. Scope rules

- **Globals.** The top level of the file is the global scope.
- **Functions.**
  - Each function call gets its own local variables: its parameters and the variables it declares.
  - A function sees its own variables and the globals, never its caller's variables.
  - Changes a function makes to a global are kept.
  - A function must not be *called* before the globals it uses are declared (checked at compile time). It can be *defined* anywhere.
  - A parameter may have the same name as a global (the parameter wins inside the function). A local variable may not.
- **Blocks don't create a new scope.**
  - A name declared inside an `if`, loop or `check` block can't be  reused anywhere else in the same function or file.
  - The name also can't be *used* after its block ends, because the block may not have run. Declare it before the block instead.
  - A `for` loop counter belongs to the enclosing scope and stays usable after the loop.
  - A `for each` variable exists only inside its loop.
- **No shadowing.**
  - A name can't be declared again while it is visible.
  - Inside a class, a parameter or local variable can't share a name with a field.
- **Classes.** Constructors and methods see their parameters, their own variables and the class's fields. They reach globals through functions.

## 7. Errors and exit codes

Errors are printed on standard error.

Lexical and syntax errors include the source line and column where the lexer
found the problem or the parser expected a token. Line and column numbers start
at 1. For example:

```text
ERROR: Line 2, column 5: unexpected character '@'
```

Parser errors point to the current token (or end of input) where parsing failed.
For an unterminated string, the location is the opening quote. Semantic errors,
such as using an undeclared variable or assigning an incompatible type, currently
report the problem but do not include a line or column. Runtime errors also
currently omit source locations. Use `--tokens` to print tokens with their
`line:column` positions when locating a token-related issue.

| Exit code | Meaning | Example |
|---|---|---|
| 0 | Program ran successfully | the program's own output |
| 1 | Compile error (lexical, syntax or semantic) | `ERROR: Line 2, column 32: 'else if' is not supported; ...` |
| 1 | Missing file or wrong extension | `ERROR: cannot open input file 'x.psk'` |
| 2 | Run-time error | `RUNTIME ERROR: division by zero` |

Run-time errors stop the program. They are:

- division or modulo by zero
- `integer overflow`
- list index out of range, or removing from an empty list
- input that can't be converted to the target's type
- a value-returning function that ends without `return`
- more than 10,000,000 iterations of one loop: `possible infinite loop`
- calls nested too deeply (about 1,000 levels of recursion, at most 10,000): `too many nested calls`
- a condition that isn't `true`/`false`, e.g. a list item used as an `if` condition

If you ever see `ERROR: internal compiler error: ...`, that is a bug in the
compiler, not in your program. Please report it.

## 8. Project structure

```text
custom_language_cpp_project/
├── include/            headers (AST, values, each compiler stage)
├── src/
│   ├── lexer.cpp         source text -> tokens
│   ├── parser.cpp        tokens -> AST (recursive descent)
│   ├── semantic.cpp      type, scope and declaration checks
│   ├── optimizer.cpp     constant folding, dead-branch removal
│   ├── ir_generator.cpp  three-address IR for --ir
│   ├── llvm_backend.cpp  LLVM IR for --llvm-ir (stubbed when built without LLVM)
│   ├── interpreter.cpp   runs the AST
│   └── main.cpp          command line, error reporting
├── tests/              test programs used by test.sh
├── demo.psk            sample program covering the main features
├── Makefile, CMakeLists.txt
├── test.sh
└── IMPLEMENTATION_GUIDE.md
```

Pipeline:

```text
source -> lexer -> parser (AST) -> semantic analysis -> AST optimizer
       -> [--ir output] [--llvm-ir output] -> interpreter
```
