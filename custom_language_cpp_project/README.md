# Plainspeak compiler and interpreter (C++)

Plainspeak is a small, English-like programming language. This project checks a
Plainspeak program (`.psk` file) completely at compile time, then runs it in a
tree-walking interpreter. It can also print the program's tokens, a
three-address intermediate representation, or optimised LLVM IR.

- **Language reference:** the *Plainspeak User Guide* (LMTE Assignment 2b) is the
  specification. This README summarises it and records what changed in the bug-fix
  release.
- **How each compiler stage works:** [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md).

## Contents

1. [Quick start](#1-quick-start)
2. [Building](#2-building)
3. [Running a program](#3-running-a-program)
4. [Testing](#4-testing)
5. [Language overview](#5-language-overview)
6. [Scope rules](#6-scope-rules)
7. [Errors and exit codes](#7-errors-and-exit-codes)
8. [Changes in this release](#8-changes-in-this-release)
9. [Project structure](#9-project-structure)

## 1. Quick start

```bash
cd custom_language_cpp_project
make                       # builds ./customlang
./customlang demo.psk      # runs a program
./test.sh                  # runs the test suite
```

## 2. Building

Requirements:

- a C++17 compiler (`clang++` or `g++`)
- `make`, or CMake 3.16+
- *optional:* the LLVM development libraries (`llvm-config`), needed only for the
  `--llvm-ir` option

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
| `LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config make` | Use a specific LLVM install. |
| `make NO_LLVM=1` | Build without LLVM even if it is installed. |
| `make run` | Build, then run `demo.psk`. |
| `make test` | Build, then run `./test.sh`. |

After installing LLVM, or switching between LLVM and non-LLVM builds, run
`make clean`. Header dependencies are tracked automatically, so editing a `.hpp`
file rebuilds every source file that includes it.

Install LLVM:

```bash
brew install llvm            # macOS (Homebrew)
sudo apt install llvm-dev    # Ubuntu / Debian
```

With CMake:

```bash
cmake -S . -B build
cmake --build build
./build/customlang demo.psk
```

Build outputs (`*.o`, `*.d`, `customlang`, `build/`) are not stored in git.
Always build the compiler on your own machine.

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

The script rebuilds from scratch, then runs every program in `tests/`. Each
program is checked in one of three ways:

- **Positive:** it must print an exact output.
- **Negative:** it must be rejected with the expected error.
- **Inspection:** its `--ir` or `--llvm-ir` output must contain what's expected.

The script stops at the first failure and prints the expected and actual output.
On success it prints `PASS: all tests`. If the build has no LLVM, the LLVM checks
print `SKIP` instead of failing.

To check your own program, run `./customlang yourfile.psk`. To check that a
mistake is caught, look for an `ERROR:` line and exit code 1 (`echo $?`).

## 5. Language overview

The User Guide is the full reference. This section is a quick reminder.

```text
// Comments start with //
int score = 10                    // declare and assign (int, integer, float, string, bool)
initialize string name            // declare with a default value
set score to score + 5            // assign (also: score = score + 5)
int limit = 99
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
for (int i = 0; i < 3; i++) { say i }
for (i = 0; i < 3; i++) { say i }  // reuse the counter in a later loop

initialize list values             // lists grow, hold mixed types, index from 0
put 10 on values
say item of values at 0
say length of values
say top of values
for each v in values { say v }     // stop / skip work in every loop
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

- **Strict strings.** A string variable can only be given a quoted literal or
  another string variable (guide §5.4). `string s = "a" + "b"` and
  `string s = name()` are rejected. Joined strings can still be printed.
- **Strict types.** Types must match. An int may go into a float, never the
  reverse; this also applies to `add`/`subtract`/... commands. Conditions must
  be `true`/`false` values.
- **Object variables.** An object variable can only hold an object of its own
  class or a subclass.
- **Constructors.**
  - A class with no constructor uses its parent's.
  - A constructor can call the parent constructor with `super(...)` as its first
    statement.
  - Without `super`, the parent's no-argument constructor runs automatically
    first.
  - If the parent constructor needs arguments, `super(...)` is required.
- **Methods.** Methods can use the object's fields, including list fields
  (`put 1 on items`), but can't call another method of the same object.
- **Reserved words.** Keywords can't be used as names. These are:
  - types and values: `int`, `integer`, `float`, `string`, `bool`, `list`,
    `nothing`, `true`, `false`
  - operators: `and`, `or`, `not`, `mod`, `is`, `of`
  - every statement keyword (`say`, `set`, `if`, `while`, `define`, …)
  - list words: `item`, `length`, `top`
  - class words: `public`, `private`, `inherits`, `constructor`, `returning`
- **List indexes.** `item of l at i + 1` uses `i + 1` as the index. A comparison
  after it applies to the item: `item of l at 0 > 5` compares the item with 5.

## 6. Scope rules

- **Globals.** The top level of the file is the global scope.
- **Functions.**
  - Each function call gets its own local variables: its parameters and the
    variables it declares.
  - A function sees its own variables and the globals, never its caller's
    variables.
  - Changes a function makes to a global are kept.
  - A function must not be *called* before the globals it uses are declared
    (checked at compile time). It can be *defined* anywhere.
  - A parameter may have the same name as a global (the parameter wins inside
    the function). A local variable may not.
- **Blocks don't create a new scope.**
  - A name declared inside an `if`, loop or `check` block can't be reused
    anywhere else in the same function or file.
  - The name also can't be *used* after its block ends, because the block may
    not have run. Declare it before the block instead.
  - A `for` loop counter belongs to the enclosing scope and stays usable after
    the loop.
  - A `for each` variable exists only inside its loop.
- **No shadowing.**
  - A name can't be declared again while it is visible.
  - Inside a class, a parameter or local variable can't share a name with a
    field.
- **Classes.** Constructors and methods see their parameters, their own
  variables and the class's fields. They reach globals through functions.

## 7. Errors and exit codes

Errors are printed on standard error.

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
- more than 10,000 nested calls: `too many nested calls`
- a condition that isn't `true`/`false`, e.g. a list item used as an `if` condition

If you ever see `ERROR: internal compiler error: ...`, that is a bug in the
compiler, not in your program. Please report it.

## 8. Changes in this release

This release fixes every issue in the *Plainspeak Compiler Bug Report* (24
issues), plus further bugs found while fixing them. The User Guide was treated as
the specification:

- **Behaviour that broke the guide's rules:** fixed.
- **Behaviour the guide documents as intended:** kept, but made safe and given a
  clearer error message. This applies to strict strings, no `else if`, one
  English comparison per condition, and no block scope.

Each fix has a regression test in `tests/`, run by `test.sh`.

### Bug report issues

| # | Issue | Fix |
|---|---|---|
| 1 | Functions saw their caller's variables (dynamic scoping) | Each call has its own frame; globals are kept in a separate frame. |
| 2 | Inheritance cycle crashed or hung the compiler | Hierarchy is validated first: `inheritance cycle involving class 'B'`. |
| 3 | Missing grandparent class gave an internal runtime error | `base class 'Missing' is not declared` (exit 1). |
| 4 | Integer overflow was undefined behaviour | Checked arithmetic: `RUNTIME ERROR: integer overflow`. |
| 5 | Methods couldn't reach globals, even through a function | Fixed by the separate global frame (#1). |
| 6 | Second `for (int i ...)` rejected | Still rejected (guide: blocks don't create a scope); the error now suggests `for (i = 0; ...)`. |
| 7 | Block variables passed checking but failed at run time | Using a variable after the block that declared it is now a compile error. |
| 8 | Local variable couldn't reuse a global's name | Still rejected (guide: no shadowing), with a clear message. |
| 9 | Function could use a global declared after the call | Compile error: `global variable 'later' is used by function 'f' before it is declared`. |
| 10 | A Dog variable could hold a Cat | Object assignment must be the same class or a subclass (checked at compile time and run time). |
| 11 | Large integers compared as doubles | Integers are compared exactly (interpreter and optimizer). |
| 12 | `a == a` was false for lists and objects | Lists and objects compare by reference. |
| 13 | `add 0.9 to x` silently truncated an int | Compile error; int commands use exact integer arithmetic. |
| 14 | A parameter named like a field hid the field | Compile error for parameters and locals that share a field's name. |
| 15 | `else if` gave a confusing error | Still unsupported (guide); the error explains how to nest an `if`. |
| 16 | A leading `(` was assumed to wrap the whole condition | `if (a) and (b)` and `if (a + b) * 2 > 9` now work. |
| 17 | English comparisons combined with `and`/`or` gave a confusing error | Still unsupported (guide); the error shows the symbol form. |
| 18 | List fields couldn't be used in methods | `put`, `remove`, `item/length/top of` work on fields. |
| 19 | Child constructor skipped the parent constructor | The parent's no-argument constructor runs automatically; `super(...)` is required if it needs arguments. |
| 20 | String rule rejected string-typed expressions | Unchanged: the guide (§5.4) makes this rule deliberate. |
| 21 | Oversized number literal reported as a runtime error | `Line 1, column 5: integer literal out of range` (exit 1). |
| 22 | Constant zero check used real division | Uses integer division, so `10 / (1 / 2)` is caught at compile time. |
| 23 | Keywords weren't reserved | Reserved words are rejected as names (see section 5). |
| 24 | Build system | Header dependency tracking; LLVM optional; build outputs removed from git and ignored. |

### Other bugs fixed

- A function called from a method could read the object's fields instead of
  globals.
- `int x = x + 1` passed checking and then failed at run time; the initializer
  is now checked before the variable exists.
- `item of l at 0 > 5` failed because the index absorbed `> 5`. The index is now
  arithmetic only.
- The optimizer folded large integer comparisons through `double` (same flaw as
  #11).
- Infinite recursion crashed the process. It now stops with
  `too many nested calls`, and the compiler runs on a large-stack thread.
- `repeat` had no 10,000,000-iteration limit, and a huge float count was
  undefined behaviour.
- Non-boolean values (e.g. list items) were silently accepted as conditions.
- `divide x by 0` / `mod x by 0` with a constant zero is now a compile error,
  like `/`.
- `put f() on l` with a `nothing` function, and other uses of a `nothing`
  result, are rejected.
- `check` rejects `when` cases from a different enum, and values that can't be
  checked (lists, objects).
- Lexer errors now include the column number.
- A missing source file exits with code 1 (compile error) instead of 2.
- Unexpected internal failures during checking are reported as internal compiler
  errors, not as run-time errors.

### Changes to the language worth knowing about

These programs behaved differently before and are now rejected or fixed:

- Keywords used as variable names, e.g. `int true = 5`.
- A parameter or local variable with the same name as a class field.
- Using a block's variable after the block ends.
- A child constructor that doesn't call `super(...)` when the parent constructor
  needs arguments.
- Conditions that aren't `true`/`false` (now a run-time error).

## 9. Project structure

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
├── demo.psk, oop.psk, division.psk, example.psk   sample programs
├── bad_type.psk, bad_uninit.psk                   deliberately invalid samples
├── Makefile, CMakeLists.txt
├── test.sh
└── IMPLEMENTATION_GUIDE.md
```

Pipeline:

```text
source -> lexer -> parser (AST) -> semantic analysis -> AST optimizer
       -> [--ir output] [--llvm-ir output] -> interpreter
```
