# Custom Language Implementation Guide

This document describes how a `.psk` source file moves through the project,
what the current language implements, and where the interpreter and LLVM
backend differ. It reflects the implementation in the source tree; it does not
assume that every feature accepted by the interpreter is also supported by the
LLVM backend.

## 1. End-to-end flow

The executable is driven by `src/main.cpp`. For a normal invocation, the flow
is:

```text
.psk file
   |
   v
Read source into a string
   |
   v
Lexer: characters -> tokens
   |
   v
Parser: tokens -> AST
   |
   v
Semantic analysis: validate declarations, types, and use
   |
   v
AST optimization: fold constants and remove known-dead branches
   |                               |
   |                               +--> optional simple IR display (--ir)
   |                               |
   |                               +--> optional LLVM lowering and O2 (--llvm-ir)
   v
Interpreter executes the optimized AST
```

The LLVM option is an additional inspection/compilation path, not the normal
execution engine. It prints optimized LLVM IR. This program does not currently
emit an object file or machine-code executable. After the optional displays,
the program still executes using the AST interpreter.

### Method used at each stage

| Stage | Method |
|---|---|
| Lexing | Hand-written, deterministic character scanner. It recognizes token patterns with lookahead and scanning loops;|
| Parsing | Recursive-descent parser. Parser functions consume tokens and build the AST; recursive calls handle nested constructs and separate expression functions encode operator precedence. |
| Semantic analysis | Recursive AST traversal with scoped symbol/type information. It validates declarations, expressions, and statements before execution. |
| AST optimization | Recursive AST rewriting with constant-expression evaluation and selected branch/loop simplifications. |
| Simple IR generation | Recursive AST-to-IR lowering that emits three-address-like instructions and control-flow labels. |
| LLVM IR generation | AST-to-LLVM-IR lowering for the supported subset, followed by LLVM's O2 module optimization pipeline. This path is optional. |
| Execution | Tree-walk interpretation: expressions are evaluated recursively and statements are executed from the AST. |

### Command-line behavior

```text
customlang <source-file.psk> [--tokens] [--ir] [--llvm-ir]
```

- A source path is required and must end in `.psk`.
- `--tokens` prints each token and its source line and column.
- `--ir` prints the project's simple, human-readable IR.
- `--llvm-ir` lowers the supported subset to LLVM IR, runs LLVM's O2 module
  optimization pipeline, and prints the result.
- Flags can be combined. If compilation succeeds, the interpreter runs after
  the requested output is printed.

`CompileError` is reported as `ERROR` and exits with status 1. Other
`std::exception` errors are reported as `RUNTIME ERROR` and exit with status 2.

## 2. Tokenisation

The lexer in `src/lexer.cpp` scans the source one character at a time and
produces a vector of `Token` records. Each token has a type, text, and starting
line/column position. Keywords are not separate token types: words such as
`define`, `if`, `return`, and `int` are all lexed as identifiers, then
recognized by the parser by comparing their text.

This is a hand-written deterministic scanner rather than a pushdown automaton:
it uses character tests, scanning loops, and limited lookahead, but does not
need a stack to recognize tokens.

### Token categories

- **Identifiers:** begin with an ASCII alphabetic character or `_`; following
  characters may also be digits. Examples: `value`, `_counter`, `item2`.
- **Numbers:** digits with an optional decimal point, including a leading
  decimal point if followed by a digit. A leading sign is a separate unary
  operator, not part of the numeric token.
- **Strings:** double-quoted text. The lexer interprets `\n`, `\t`, `\"`, and
  `\\`; other escaped characters are taken as the escaped character. Newlines
  can occur inside the scanned string. Unterminated strings are compile errors.
- **Operators and punctuation:** arithmetic (`+`, `-`, `*`, `/`), assignment
  (`=`), comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`), increment (`++`),
  parentheses, braces, commas, semicolons, and `<<`.
- **Comments:** `//` starts a comment that continues to the end of the line.
- **Whitespace:** skipped while line and column positions are updated.
- **End token:** an explicit end-of-input token terminates parsing.

Unexpected characters and unterminated strings produce compile errors with
source location information. The lexer does not define exponentiation (`^`),
single-quoted strings, or block comments.

## 3. Parsing and the AST

`src/parser.cpp` uses recursive-descent parsing. Its functions consume tokens
and call one another to recognize nested syntax; the expression parsing
functions encode operator precedence. It checks syntax and creates the node
types declared in `include/ast.hpp`. Expressions and statements are represented
by separate AST hierarchies. Nodes own their child nodes through
`std::unique_ptr`, so the parsed tree has a clear single owner.

The parser handles both conventional forms and selected English-style
commands, for example `x = x + 1`, `set x to x + 1`, and `add 1 to x`.
Semicolons are generally optional.

### Expression precedence and evaluation order

The parser builds the tree using these precedence levels, from tightest to
loosest:

1. Parenthesized expressions and primary values: literals, names, function
   calls, member access, list operations.
2. Unary operators: `+`, `-`, `not`.
3. Multiplication, division, modulo: `*`, `/`, `mod`.
4. Addition and subtraction: `+`, `-`.
5. Ordering comparisons: `<`, `<=`, `>`, `>=`.
6. Equality: `==`, `!=`.
7. Boolean `and`.
8. Boolean `or`.

Operators at the same binary precedence level are grouped left to right. Thus
`20 / 5 * 2` groups as `(20 / 5) * 2`, while `2 + 3 * 4` groups as
`2 + (3 * 4)`. Parentheses can override the standard grouping. `^` is not a
supported exponent operator.

### Statements and declarations

The AST represents declarations, initialization, assignments, output, input,
arithmetic updates, calls, returns, blocks, conditionals, loops, selection,
list mutations, and loop control. Top-level type/function nodes include
classes, structs, enums, and functions.

Supported control flow includes:

- `if` / `else` (also `otherwise`);
- `while`;
- `repeat N times`;
- C-style `for (initialize; condition; increment)`;
- `for each name in collection`;
- `check` / `when` / `otherwise`;
- `stop` and `skip` for exiting or continuing the innermost loop.

The parser translates natural-language conditions such as “is greater than”
and “is not equal to” into the same comparison-expression nodes used by
symbolic operators.

## 4. Semantic analysis

`src/semantic.cpp` runs over the AST before optimization or execution. It
collects top-level declarations and checks whether program structure and
expressions are valid. Invalid programs fail before the interpreter runs.

### Type system

Built-in types are `int`/`integer`, `float`, `string`, `bool`, `list`, and
`nothing` for a void function result. The runtime also has object, enum, and
`Any` value categories. Assignment permits exact matches, `int` to `float`,
and values involving `Any` where the dynamic list/function behavior needs
flexibility.

The analyzer checks, among other things:

- duplicate and undeclared variables, types, functions, fields, and methods;
- use of variables before declaration;
- assignment compatibility and initialization;
- numeric operands for arithmetic and unary numeric operators;
- boolean conditions for `if`, `while`, and `for`;
- function and constructor argument count and types;
- return values against declared function return types;
- input prompt and target types;
- enum ownership/type consistency;
- list operation targets and integer list indices;
- fixed-variable updates;
- `stop`/`skip` placement inside loops;
- class inheritance, constructors, access controls, method signatures, and
  abstract-method implementation.

Constant division or modulo by zero is rejected during semantic analysis,
including expressions such as `10 / (3 - 3)`. Division by a value that is only
known at runtime is checked at runtime.

Semantic analysis precedes optimization intentionally. Code that is
semantically invalid is still rejected even if a later constant-condition
optimization could remove its branch.

### Generic functions

Top-level functions support inferred generic type parameters:

```text
define function identity<T>(T value) returning T {
    return value
}

say identity(42)
say identity("hello")
```

Type parameters are written in angle brackets after the function name and can
be used for parameter and return types. At a call, the analyzer infers types
from arguments. Arguments assigned to the same generic type parameter must
agree; for example, `list<T>(T a, T b)` cannot be called with one integer and
one string. A generic return value must preserve the declared generic type.

Generics are currently limited to top-level functions. Generic classes,
generic methods, explicit type arguments such as `identity<int>(42)`, generic
constraints, and type parameters inferred only from a return type are not
implemented. Generic functions run in the interpreter; LLVM lowering does not
support functions yet.

## 5. AST optimization

The optimizer in `src/optimizer.cpp` runs after semantic analysis and before
the IR display and interpreter execution. It recursively rewrites expressions
and statement bodies.

### Implemented transformations

- **Constant folding:** evaluates supported constant expressions at compile
  time. This covers integer and floating-point arithmetic, comparisons,
  boolean operations, unary operations, string concatenation, and supported
  string comparisons.
- **Constant condition simplification:** replaces an `if` whose condition
  folds to a boolean literal with the selected branch (or an empty block if
  there is no selected branch).
- **Dead-loop removal:** replaces `while (false)` with an empty block.

Integer constant arithmetic is guarded against signed overflow; if a fold
would overflow, the optimizer leaves the expression intact rather than
performing an unsafe host-language calculation. Division/modulo cases that
cannot be folded safely are also left for normal runtime behavior or the
semantic diagnostic.

This is a small AST optimization pass, not a complete dead-code-elimination
framework. It does not currently propagate variable constants, remove
statements after `return`, unroll loops, or optimize all loop forms. In
addition, the optimizer must preserve language evaluation behavior; it only
folds expressions it can establish as constants.

### LLVM's separate optimization

If `--llvm-ir` is requested, the LLVM backend independently constructs LLVM IR
for supported program constructs and runs LLVM's default O2 module pipeline.
This is separate from the AST optimizer above. O2 may apply optimizations such
as simplification and dead-code elimination to the generated LLVM module.

## 6. Intermediate representation

`src/ir_generator.cpp` emits a simple textual, three-address-like
representation for inspection with `--ir`. It gives intermediate expression
results temporary names such as `%t1` and uses labels and conditional jumps
for control flow. It is not the executable representation used by the normal
interpreter.

For example, an arithmetic expression may appear as:

```text
%t1 = 3 * 4
%t2 = 2 + %t1
PRINT %t2
```

The IR is generated after AST optimization, so constant expressions and
branches may already have been simplified.

## 7. Interpreter and runtime behavior

`src/interpreter.cpp` evaluates AST expressions and executes statements. It
stores variable metadata and values in an interpreter context. Function calls
install parameters/local values and restore the caller's state after return.
Objects contain named fields; methods execute with an active object so method
bodies can access fields.

### Values, lists, and memory

`Value` supports integer, floating-point, string, boolean, void, object, enum,
list, and `Any` values. Object and list payloads use `std::shared_ptr`:

- acyclic objects/lists are released when the last shared owner goes away;
- this is reference counting through the C++ standard library, not a tracing
  garbage collector;
- cycles of object/list references can keep reference counts nonzero and are
  not collected by the current runtime.

Lists are dynamically sized vectors and may contain different value types.
Operations include append, remove-last, item access, top, length, and
iteration. Indexing is zero-based; an invalid index or removing from an empty
list raises a runtime error.

### Runtime checks and errors

The interpreter checks operations that depend on runtime values, including
division/modulo by zero, list bounds/emptiness, failed conversions, invalid
input, and unresolved variables or fields. While/repeat/foreach execution has
a high iteration guard to report possible infinite loops.

## 8. Object-oriented features

Classes support:

- fields and methods;
- public/private member visibility;
- constructors with typed parameters;
- single inheritance;
- method overriding and dynamic dispatch through the instance's class
  hierarchy;
- abstract classes and abstract methods.

The semantic analyzer checks visibility and class/method consistency,
including abstract method requirements for concrete subclasses. The
interpreter allocates an object, initializes inherited and declared fields,
runs constructors, and resolves a method starting at the runtime class before
walking base classes.

Structs provide typed fields with default initialization but do not have class
inheritance or methods. Enums define named members with integer indices.

## 9. LLVM backend and compilation boundary

`src/llvm_backend.cpp` builds LLVM IR for a narrower subset than the
interpreter. The implemented path covers scalar integer, float, boolean, and
string values; arithmetic and comparisons; output; and supported conditional
and loop forms. It verifies the LLVM module before running O2.

The LLVM backend reports unsupported constructs explicitly. Current
unsupported areas include classes/objects, structs, enums, input, functions
(including generic functions), lists, `check`, logical operators, modulo, and
string concatenation. The exact accepted subset is governed by backend
lowering; passing semantic analysis does not imply LLVM support.

The backend returns optimized LLVM IR as text. It does not invoke a target
code generator, linker, or JIT in the current driver. Therefore the project
currently has an LLVM IR generation/optimization path, not a full native
machine-code output path.

## 10. Current scope at a glance

| Area | Current implementation |
|---|---|
| Lexing and parsing | Source positions, comments, strings, literals, expressions, statements, and AST construction |
| Semantic analysis | Static checks for declarations, many types, functions, OOP, control flow, and constant zero divisors |
| Expression precedence | Unary, multiplicative, additive, comparison, equality, `and`, then `or` |
| Optimization | AST constant folding, constant boolean branch selection, and removal of false while loops |
| LLVM optimization | LLVM O2 pipeline over the backend-supported subset |
| Object orientation | Classes, encapsulation, single inheritance, constructors, abstract methods, overriding, dynamic dispatch |
| Generics | Inferred type parameters on top-level functions |
| Dynamic allocation | C++ `shared_ptr` objects and vector-backed lists |
