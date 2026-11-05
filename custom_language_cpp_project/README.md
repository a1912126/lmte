# Custom Language Compiler / Interpreter (C++)

This project implements the custom language as a small compiler/interpreter pipeline.

For a detailed description of tokenisation, parsing, semantic analysis,
optimization, runtime features, and LLVM backend scope, see
[IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md).

## Project structure

```text
custom_language_cpp_project/
├── include/
│   ├── ast.hpp
│   ├── common.hpp
│   ├── interpreter.hpp
│   ├── ir_generator.hpp
│   ├── llvm_backend.hpp
│   ├── lexer.hpp
│   ├── parser.hpp
│   ├── semantic.hpp
│   ├── token.hpp
│   └── value.hpp
├── src/
│   ├── interpreter.cpp
│   ├── ir_generator.cpp
│   ├── lexer.cpp
│   ├── llvm_backend.cpp
│   ├── main.cpp
│   ├── parser.cpp
│   ├── semantic.cpp
│   ├── token.cpp
│   └── value.cpp
├── tests/
├── CMakeLists.txt
├── Makefile
├── demo.psk
└── test.sh
```

## Compiler/interpreter pipeline

```text
Source program
     |
     v
Lexer / lexical analysis
     |
     v
Tokens
     |
     v
Recursive-descent parser
     |
     v
AST
     |
     v
Semantic analysis
     |
     v
AST optimization (constant folding and constant-branch elimination)
     |
     v
AST interpreter (normal execution)
     |
     └── optional LLVM IR lowering → O2 optimization → print optimized IR
```

## Build

Using Make:

```bash
make
```

Or directly (with `llvm-config` available):

```bash
clang++ -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude \
    -I"$(llvm-config --includedir)" \
    src/main.cpp src/token.cpp src/lexer.cpp src/value.cpp src/parser.cpp \
    src/semantic.cpp src/interpreter.cpp src/ir_generator.cpp src/llvm_backend.cpp \
    $(llvm-config --ldflags --libs core passes support --system-libs) \
    -o customlang
```

Using CMake:

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./customlang demo.psk
```

Show lexical tokens:

```bash
./customlang demo.psk --tokens
```

Show intermediate representation:

```bash
./customlang demo.psk --ir
```

Show optimized LLVM IR:

```bash
./customlang demo.psk --llvm-ir
```

The LLVM backend lowers scalar integer, float, bool, and string programs with
arithmetic, comparisons, output, conditionals, and loops, then runs LLVM's O2
module optimization pipeline. The optimized LLVM IR is displayed; program
execution still uses the existing interpreter. Unsupported constructs (such
as classes, structs, enums, input, functions, lists, switch cases, logical
operators, modulo, and string concatenation) are reported
explicitly by the LLVM backend. The normal interpreter path does not require
those constructs to be lowered to LLVM IR.

`--ir` and `--llvm-ir` can be used together. Both display their IR before the
program runs in the interpreter.

Plainspeak source files must use the `.psk` extension.

After semantic analysis, the AST optimizer folds constant scalar expressions
and removes branches guarded by constant boolean conditions (including loops
whose condition is always false). Semantic analysis runs first, so invalid
code in a branch that will later be removed is still diagnosed. The LLVM
backend separately runs LLVM's O2 optimization pipeline.

LLVM development libraries and `llvm-config` are required to build the project.
On macOS with Homebrew, install them with `brew install llvm`; if needed, build
with `LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config make`.

## Datatypes and declaration

Built-in datatypes are:

```text
int
integer
float
string
bool
list
```

Declaration with a value:

```text
int x = 5
float y = 2.5
string message = "hello"
```

A variable can also be declared using the required initialization command:

```text
initialize x int
initialize y float
initialize message string
```

Assignments and compound arithmetic require the target variable to have been initialized first.

## String assignment rule

String values are deliberately strict:

A quoted value is a string literal:

```text
string message = "hello"
set message to "world"
```

An unquoted identifier is always interpreted as a variable name, never as a string literal:

```text
initialize first string
set first to "hello"
initialize second string
set second to first
```

Therefore this is invalid:

```text
string message = hello
```

because `hello` is treated as a variable reference and no variable named `hello` exists.

This is also invalid:

```text
initialize count int
set count to 5
initialize message string
set message to count
```

because a string variable can only receive a quoted string literal or another string variable.

## Assignment

```text
set x to 5
set x to x + 5
x = x + 5
x = x - 5
x = x * 5
x = x / 5
```

English-style commands:

```text
add 5 to x
subtract 5 from x
multiply 5 to x
divide x by 5
```

## Output

```text
cout << x
say x
say "hello"
```

`ask` prints a string prompt, reads one line, and converts it to the type of
the already initialized target variable:

```text
initialize int age
ask "Enter your age: " into age
say age
```

Integer, float, string, and bool targets are supported. Bool input must be
`true` or `false`; invalid input and undeclared targets produce errors.

## Conditions

C++-style:

```text
if (x > 5) { ... }
if (x < 5) { ... }
if (x == 5) { ... }
```

Natural-language form:

```text
if x is greater than 5 { ... }
if x is less than 5 { ... }
if x is equal to 5 { ... }
```

Alternatives:

```text
else { ... }
otherwise { ... }
```

## Loops

```text
while (x < 10) { ... }
while x is less than 10 do { ... }
repeat 5 times { ... }
for (int i = 0; i < 5; i++) { ... }
for each item in values { ... }
```

`stop` exits the innermost loop and `skip` continues with its next iteration.

## Functions and return values

Top-level functions accept typed parameters and can return a typed value or
`nothing`. They can be called in expressions or as statements:

```text
define function double(integer value) returning integer {
    return value * 2
}

say double(5)
```

`return` without a value is valid in a `nothing` function.

Generic top-level functions use type parameters in angle brackets. The
interpreter infers each type parameter from the call arguments and checks that
arguments sharing a type parameter have the same type:

```text
define function identity<T>(T value) returning T {
    return value
}

say identity(42)
say identity("generic")
```

Generic class types and explicit type arguments are not supported. The LLVM
backend currently does not lower functions; generic functions run through the
interpreter.

## Selection, logical operators, and constants

`check` selects the first matching `when` block, or the optional `otherwise`
block. Cases do not fall through:

```text
check choice {
    when 1 { say "one" }
    when 2 { say "two" }
    otherwise { say "other" }
}
```

The natural-language comparisons support `is not equal to`,
`is greater than or equal to`, and `is less than or equal to`, in addition to
the existing equality and strict-order comparisons. Expressions also support
`and`, `or`, `not`, and integer or floating-point `mod`.
The command form `mod variable by divisor` also updates an initialized
integer or float variable.

Use `fix name` after declaring a variable to make it constant. Later assignment,
input, and compound updates to that variable are rejected; fixed lists cannot
be modified.

## Lists

Lists are dynamically sized and can hold values of different types. They use
the standard library vector implementation:

```text
initialize list values
put 10 on values
put "hello" on values
say item of values at 0
say top of values
say length of values
remove item of values

for each value in values {
    say value
}
```

`remove item of name` removes the last item. Indexing is zero-based, and
out-of-range access or removing from an empty list produces a runtime error.
There is no `capacity` language operation.

## Classes and object-oriented features

Classes support public/private fields and methods, one parent class, constructors,
method overriding, and abstract classes/methods:

```text
abstract define class Animal {
    public initialize string name
    public abstract define function speak() returning nothing
    public define constructor {string who} {
        set name to who
    }
}

define class Dog inherits Animal {
    private initialize integer age
    public define constructor {string who, integer years} {
        super(who)
        set age to years
    }
    public define function speak() returning nothing {
        say "Dog Noise"
    }
}

Initialize Dog pike with ("Pike", 1)
call speak() from pike
say name of pike
```

Methods can read and update their active object's fields directly. Calling an
overridden method on an instance dispatches to the most-derived implementation.
Abstract classes cannot be instantiated; a concrete subclass must implement
inherited abstract methods. Class members are private by default. Public fields
can also be updated with `set field of instance to expression`.

A child constructor can call its inherited constructor with `super(arguments)`
as its first statement. Use `super()` when the inherited constructor takes no
arguments. If an intermediate parent has no constructor, the call continues up
the inheritance chain to the nearest ancestor constructor.

## Structs and enums

Structs have typed, public fields and are initialized with default field values:

```text
Define struct Point {
    integer x
    float y
}

initialize Point p
set x of p to 3
say x of p
```

Enums define integer constants starting at zero. Enum variables can be assigned
those members and compared with them:

```text
Define enum Colour {red, green, blue}
initialize Colour colour
set colour to green
say colour
```

## Semantic checks

The semantic analyser checks:

- duplicate declarations;
- use of undeclared variables;
- assignment to variables that were not initialized first;
- `int`, `float`, and `string` type compatibility;
- numeric-only arithmetic;
- Boolean conditions for `if` and `while`;
- numeric repeat counts;
- function argument and return types;
- list operations and loop-control placement;
- assignment to fixed variables;
- string literal vs string-variable assignment rules.
- constant division or modulo by zero, including constant arithmetic
  expressions such as `10 / (3 - 3)`;
- generic function argument types and generic type inference.

Boolean values can be declared with `bool`, read with `ask`, and written as the
`true` and `false` literals. Comparisons and logical operators also produce
boolean values.

## Intermediate representation

The IR generator emits simple three-address-like instructions such as:

```text
DECLARE int x
x = 5
%t1 = x + 5
x = %t1
PRINT x
IF_FALSE %t2 GOTO else1
```

The generated IR is currently available for inspection with `--ir`. Program execution is performed by the AST interpreter.

## Tests

Run:

```bash
./test.sh
```

The tests include valid quoted string assignment, valid assignment from another string variable, and invalid unquoted-string/type cases.
