#!/usr/bin/env bash
set -u

make -s clean
make -s || { echo "FAIL: build"; exit 1; }

# A build without LLVM (see Makefile) cannot run the --llvm-ir checks; skip them.
llvmAvailable=1
if ./customlang tests/llvm_backend.psk --llvm-ir 2>&1 >/dev/null | grep -q "compiled without LLVM"; then
    llvmAvailable=0
fi

# expect_output <file> <expected stdout>: the program must run (exit 0) and print exactly this.
expect_output() {
    local actual
    actual="$(./customlang "$1" 2>&1)"
    local status=$?
    if [[ $status -ne 0 || "$actual" != "$2" ]]; then
        echo "FAIL: $1 (exit $status)"
        printf 'Expected:\n%s\nActual:\n%s\n' "$2" "$actual"
        exit 1
    fi
}

# expect_error <file> <exit code> <message fragment>: the program must fail with this
# exit code (1 = compile error, 2 = run-time error) and mention the fragment.
expect_error() {
    local actual
    actual="$(./customlang "$1" 2>&1)"
    local status=$?
    if [[ $status -ne $2 ]] || ! grep -Fq -- "$3" <<<"$actual"; then
        echo "FAIL: $1 should exit $2 with an error containing: $3"
        printf 'Actual (exit %s):\n%s\n' "$status" "$actual"
        exit 1
    fi
}

expected=$'hello\nchanged'
actual="$(./customlang tests/string_variable.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: string variable assignment"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

actual="$(./customlang tests/string_literal.psk)"
if [[ "$actual" != "hello" ]]; then
    echo "FAIL: quoted string literal"
    exit 1
fi

stringIR="$(./customlang tests/string_ir_escaping.psk --ir)"
if ! grep -Fq 'PRINT "line1\n\"quoted\"\\path"' <<<"$stringIR"; then
    echo "FAIL: string literals should be escaped in generated IR"
    printf '%s\n' "$stringIR"
    exit 1
fi

for file in tests/string_unquoted_literal.psk tests/string_wrong_variable_type.psk tests/string_expression.psk; do
    if ./customlang "$file" >/dev/null 2>&1; then
        echo "FAIL: $file should be rejected"
        exit 1
    fi
done

expected=$'Dog Noise\nPike\nPike\nRex'
actual="$(./customlang tests/classes.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: class inheritance, constructor, method field access, and overriding"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

expected=$'3\n1'
actual="$(./customlang tests/class_fix_scope.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: fixed variables in class methods should not leak into other scopes"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

for file in tests/class_duplicate_method_parameters.psk tests/class_duplicate_constructor_parameters.psk; do
    if ./customlang "$file" >/dev/null 2>&1; then
        echo "FAIL: $file should reject duplicate parameters"
        exit 1
    fi
done

expected=$'7\n0\n9\n5'
actual="$(./customlang tests/super_constructor.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: explicit super constructor invocation and inherited constructor chaining"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

for file in tests/super_constructor_wrong_arity.psk tests/super_constructor_in_method.psk tests/super_constructor_not_first.psk; do
    if ./customlang "$file" >/dev/null 2>&1; then
        echo "FAIL: $file should reject invalid super constructor usage"
        exit 1
    fi
done

if ./customlang tests/private_field.psk >/dev/null 2>&1; then
    echo "FAIL: private field access should be rejected"
    exit 1
fi

if ./customlang tests/abstract_method_missing.psk >/dev/null 2>&1; then
    echo "FAIL: concrete class missing an abstract method should be rejected"
    exit 1
fi

expected=$'3\n4.5\n0\n1\ngreen selected'
actual="$(./customlang tests/struct_enum.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: struct fields and enum constants"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

expected=$'8\n120\nfinished\n7\ntrue\n1\n2\nnot equal\ngreater or equal\nlogical\nor\nless or equal\nselected\n2\n20\n20\n20\n1\n7'
actual="$(./customlang tests/missing_syntax.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: functions, booleans, logical/modulo operators, selection, lists, and loop control"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

if ./customlang tests/fixed_assignment.psk >/dev/null 2>&1; then
    echo "FAIL: assignment to a fixed variable should be rejected"
    exit 1
fi

if ./customlang tests/loop_control_outside_loop.psk >/dev/null 2>&1; then
    echo "FAIL: loop control outside a loop should be rejected"
    exit 1
fi

expected=$'42\ngeneric\n7'
actual="$(./customlang tests/generic_functions.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: generic function inference and execution"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

if ./customlang tests/generic_type_mismatch.psk >/dev/null 2>&1; then
    echo "FAIL: generic function type mismatch should be rejected"
    exit 1
fi

if ./customlang tests/generic_invalid_return.psk >/dev/null 2>&1; then
    echo "FAIL: generic function with an incompatible return should be rejected"
    exit 1
fi

if ./customlang tests/constant_division_by_zero.psk >/dev/null 2>&1; then
    echo "FAIL: constant division by zero should be rejected semantically"
    exit 1
fi

expected="20"
actual="$(./customlang tests/optimization.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: constant folding and dead branch elimination"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

optimizedIR="$(./customlang tests/optimization.psk --ir)"
if grep -q 'IF_FALSE\|unreachable' <<<"$optimizedIR" || ! grep -q 'PRINT 20' <<<"$optimizedIR"; then
    echo "FAIL: optimized IR should contain folded output and no dead branches"
    printf '%s\n' "$optimizedIR"
    exit 1
fi

if ./customlang tests/missing_syntax.psk --llvm-ir >/dev/null 2>&1; then
    echo "FAIL: unsupported interpreter features should be rejected by the LLVM backend"
    exit 1
fi

expected=$'Enter your age: 23\nEnter your height: 1.75\nEnter your name: Ada Lovelace\nReady? true'
actual="$(printf '23\n1.75\nAda Lovelace\ntrue\n' | ./customlang tests/input.psk)"
if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: input conversion for integer, float, string, and bool"
    printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual"
    exit 1
fi

if printf 'not-a-number\n' | ./customlang tests/input.psk >/dev/null 2>&1; then
    echo "FAIL: invalid integer input should be rejected"
    exit 1
fi

if ./customlang tests/input_undeclared.psk >/dev/null 2>&1; then
    echo "FAIL: ask into an undeclared variable should be rejected"
    exit 1
fi

if [[ $llvmAvailable -eq 1 ]]; then
    llvmOutput="$(./customlang tests/llvm_backend.psk --llvm-ir)" || {
        echo "FAIL: LLVM backend should compile and optimize the scalar test"
        exit 1
    }
    if ! grep -q 'define .*i32 @main' <<<"$llvmOutput"; then
        echo "FAIL: LLVM IR output is missing the generated main function"
        exit 1
    fi
    if ! grep -q 'i64 5' <<<"$llvmOutput" || grep -q 'unreachable branch' <<<"$llvmOutput"; then
        echo "FAIL: LLVM O2 pipeline did not fold constants and remove dead branch"
        exit 1
    fi
else
    echo "SKIP: LLVM backend tests (this build was compiled without LLVM)"
fi

# Regression tests for the Plainspeak bug report (numbers refer to its issues).
expect_output tests/scope_function_globals.psk $'1\n5\n7'                       # 1, 5
expect_error tests/inheritance_cycle.psk 1 "inheritance cycle involving class 'B'"  # 2
expect_error tests/missing_grandparent.psk 1 "base class 'Missing' is not declared" # 3
expect_error tests/integer_overflow.psk 2 "integer overflow"                         # 4
expect_error tests/loop_counter_redeclared.psk 1 "variable 'i' already declared"     # 6
expect_output tests/loop_counter_reuse.psk $'0\n1\n0\n1'                          # 6
expect_error tests/block_variable_after_block.psk 1 "declared inside a block"        # 7
expect_error tests/local_reuses_global.psk 1 "already declared as a global"          # 8
expect_error tests/global_used_before_declared.psk 1 "global variable 'later' is used by function 'f' before it is declared" # 9
expect_error tests/object_assignment_class.psk 1 "cannot assign Cat to Dog variable 'd'" # 10
expect_output tests/object_assignment_subclass.psk $'woof\nsame object\nsame list' # 10, 12
expect_output tests/large_integers.psk $'correct\ncorrect\n9007199254740995'      # 11, 13
expect_error tests/compound_float_into_int.psk 1 "cannot update int variable 'x'"   # 13
expect_error tests/parameter_hides_field.psk 1 "same name as a field of class 'P'"  # 14
expect_error tests/else_if.psk 1 "'else if' is not supported"                       # 15
expect_output tests/condition_parentheses.psk $'both\ngrouped'                     # 16
expect_error tests/english_comparison_and.psk 1 "English comparison must be the whole condition" # 17
expect_output tests/list_field_in_method.psk $'2\n2\n1\n3\n1\n1'               # 18
expect_output tests/implicit_super.psk $'child\n5'                                 # 19
expect_error tests/super_required.psk 1 "must call super(...)"                      # 19
expect_error tests/literal_out_of_range.psk 1 "Line 1, column 5: integer literal out of range" # 21
expect_error tests/constant_integer_division_by_zero.psk 1 "division by constant zero" # 22
expect_error tests/reserved_word.psk 1 "'true' is a reserved word"                  # 23
expect_error tests/recursion_limit.psk 2 "too many nested calls"
expect_error tests/non_boolean_condition.psk 2 "condition must be a boolean"

echo "PASS: all tests"
