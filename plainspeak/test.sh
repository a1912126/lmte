#!/usr/bin/env bash
# Rebuilds customlang and runs every program in tests/ (plus demo.psk).
# Stops at the first failure and prints the expected and actual output.
set -u
cd "$(dirname "$0")"

make -s clean
make -s || { echo "FAIL: build"; exit 1; }

checked=0

fail() {
    echo "FAIL: $1"
    shift
    printf '%s\n' "$@"
    exit 1
}

# expect_output <file> <expected output> [stdin]: the program must exit 0 and print exactly this.
expect_output() {
    local actual status
    actual="$(printf '%s' "${3-}" | ./customlang "$1" 2>&1)"
    status=$?
    if [[ $status -ne 0 || "$actual" != "$2" ]]; then
        fail "$1 (exit $status)" "Expected:" "$2" "Actual:" "$actual"
    fi
    checked=$((checked + 1))
}

# expect_error <file> <exit code> <message fragment> [stdin]: the program must fail with this
# exit code (1 = compile error, 2 = run-time error) and mention the fragment.
expect_error() {
    local actual status
    actual="$(printf '%s' "${4-}" | ./customlang "$1" 2>&1)"
    status=$?
    if [[ $status -ne $2 ]] || ! grep -Fq -- "$3" <<<"$actual"; then
        fail "$1 should exit $2 with an error containing: $3" "Actual (exit $status):" "$actual"
    fi
    checked=$((checked + 1))
}

# expect_listing <description> <output> <must contain...>: inspect --tokens/--ir/--llvm-ir output.
expect_listing() {
    local description="$1" output="$2" fragment
    shift 2
    for fragment in "$@"; do
        grep -Fq -- "$fragment" <<<"$output" || fail "$description: missing '$fragment'" "$output"
    done
    checked=$((checked + 1))
}

# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------
actual="$(./customlang 2>&1)"
[[ $? -eq 1 && "$actual" == Usage:* ]] || fail "no arguments should print usage and exit 1" "$actual"
expect_error tests/does_not_exist.psk 1 "cannot open input file"
expect_error README.md 1 "must use the '.psk' extension"
expect_listing "--tokens" "$(./customlang tests/llvm_backend.psk --tokens)" \
    "=== TOKENS ===" "1:1  identifier  'initialize'"

# ---------------------------------------------------------------------------
# Demo program
# ---------------------------------------------------------------------------
demoOutput="$(printf 'Ada\n1\n' | ./customlang demo.psk 2>&1)" || fail "demo.psk should run" "$demoOutput"
expect_listing "demo.psk" "$demoOutput" \
    "Welcome to the shelter, Ada!" "Rex says Woof!" "Luna says Meow!" "roll over" \
    "Ada met this many pets:" "The shelter is open - enjoy your visit!"

# ---------------------------------------------------------------------------
# Lexical and syntax errors
# ---------------------------------------------------------------------------
expect_error tests/unexpected_character.psk 1 "Line 2, column 7: unexpected character '@'"
expect_error tests/semicolon_statement.psk 1 "unexpected token ';'"
expect_error tests/unterminated_string.psk 1 "Line 1, column 5: unterminated string literal"
expect_error tests/literal_out_of_range.psk 1 "Line 1, column 5: integer literal out of range"
expect_error tests/reserved_word.psk 1 "'true' is a reserved word"
expect_output tests/cout_identifier.psk $'42'
expect_error tests/else_if.psk 1 "'else if' is not supported"
expect_error tests/english_comparison_and.psk 1 "English comparison must be the whole condition"

# ---------------------------------------------------------------------------
# Variables, types and strings
# ---------------------------------------------------------------------------
expect_output tests/string_variable.psk $'hello\nchanged'
expect_error tests/undeclared_variable.psk 1 "variable or field 'x' is not declared"
expect_error tests/type_mismatch.psk 1 "cannot assign string to int variable"
expect_error tests/string_unquoted_literal.psk 1 "variable or field 'hello' used before declaration"
expect_error tests/string_wrong_variable_type.psk 1 "must be assigned a quoted string literal or another string variable"
expect_error tests/string_expression.psk 1 "must be assigned a quoted string literal or another string variable"
expect_error tests/compound_float_into_int.psk 1 "cannot update int variable 'x'"
expect_error tests/fixed_assignment.psk 1 "cannot assign to fixed variable 'constant'"
expect_output tests/large_integers.psk $'correct\ncorrect\n9007199254740995'
expect_error tests/integer_overflow.psk 2 "integer overflow"

# ---------------------------------------------------------------------------
# Input
# ---------------------------------------------------------------------------
expect_output tests/input.psk $'Enter your age: 23\nEnter your height: 1.75\nEnter your name: Ada Lovelace\nReady? true' \
    $'23\n1.75\nAda Lovelace\ntrue\n'
expect_error tests/input.psk 2 "invalid integer input for 'age'" $'not-a-number\n'
expect_error tests/input_undeclared.psk 1 "ask target 'age' must be a declared variable"

# ---------------------------------------------------------------------------
# Control flow, functions, lists
# ---------------------------------------------------------------------------
expect_output tests/missing_syntax.psk \
    $'8\n120\nfinished\n7\ntrue\n1\n2\nnot equal\ngreater or equal\nlogical\nor\nless or equal\nselected\n2\n20\n20\n20\n1\n7'
expect_output tests/condition_parentheses.psk $'both\ngrouped'
expect_error tests/non_boolean_condition.psk 2 "condition must be a boolean"
expect_error tests/loop_control_outside_loop.psk 1 "'stop' and 'skip' can only be used inside a loop"
expect_output tests/loop_counter_reuse.psk $'0\n1\n0\n1'
expect_error tests/loop_counter_redeclared.psk 1 "variable 'i' already declared"
expect_error tests/function_wrong_arity.psk 1 "wrong number of arguments for function 'double'"
expect_error tests/missing_return.psk 2 "function 'sign' completed without returning a value"
expect_error tests/recursion_limit.psk 2 "too many nested calls"
expect_output tests/generic_functions.psk $'42\ngeneric\n7'
expect_error tests/generic_type_mismatch.psk 1 "generic type 'T' has conflicting argument types"
expect_error tests/generic_invalid_return.psk 1 "return value must have generic type 'T'"
expect_error tests/list_index_out_of_range.psk 2 "list index out of range"
expect_error tests/list_remove_empty.psk 2 "cannot remove item from an empty list"
expect_error tests/runtime_division_by_zero.psk 2 "division by zero"
expect_error tests/constant_division_by_zero.psk 1 "division by constant zero"
expect_error tests/constant_integer_division_by_zero.psk 1 "division by constant zero"

# ---------------------------------------------------------------------------
# Scope rules
# ---------------------------------------------------------------------------
expect_output tests/scope_function_globals.psk $'1\n5\n7'
expect_error tests/block_variable_after_block.psk 1 "declared inside a block"
expect_error tests/local_reuses_global.psk 1 "already declared as a global"
expect_error tests/global_used_before_declared.psk 1 "global variable 'later' is used by function 'f' before it is declared"
expect_output tests/class_fix_scope.psk $'3\n1'

# ---------------------------------------------------------------------------
# Structs, enums and classes
# ---------------------------------------------------------------------------
expect_output tests/struct_enum.psk $'3\n4.5\n0\n1\ngreen selected'
expect_output tests/classes.psk $'Dog Noise\nPike\nPike\nRex'
expect_output tests/super_constructor.psk $'7\n0\n9\n5'
expect_output tests/implicit_super.psk $'child\n5'
expect_output tests/list_field_in_method.psk $'2\n2\n1\n3\n1\n1'
expect_output tests/object_assignment_subclass.psk $'woof\nsame object\nsame list'
expect_error tests/object_assignment_class.psk 1 "cannot assign Cat to Dog variable 'd'"
expect_error tests/private_field.psk 1 "field 'value' is private"
expect_error tests/abstract_method_missing.psk 1 "must implement abstract method 'speak'"
expect_error tests/class_duplicate_method_parameters.psk 1 "parameter 'value' is declared more than once"
expect_error tests/class_duplicate_constructor_parameters.psk 1 "parameter 'value' is declared more than once"
expect_error tests/parameter_hides_field.psk 1 "same name as a field of class 'P'"
expect_error tests/super_required.psk 1 "must call super(...)"
expect_error tests/super_constructor_wrong_arity.psk 1 "wrong number of arguments for base constructor"
expect_error tests/super_constructor_in_method.psk 1 "'super()' can only be used inside a constructor"
expect_error tests/super_constructor_not_first.psk 1 "'super()' must be the first statement in a constructor"
expect_error tests/inheritance_cycle.psk 1 "inheritance cycle involving class 'B'"
expect_error tests/missing_grandparent.psk 1 "base class 'Missing' is not declared"

# ---------------------------------------------------------------------------
# Optimiser and IR output
# ---------------------------------------------------------------------------
expect_output tests/optimization.psk $'20\n2\noptimizer demo\ntrue\nchosen branch'
optimizedIR="$(./customlang tests/optimization.psk --ir)"
if grep -q 'IF_FALSE\|unreachable' <<<"$optimizedIR"; then
    fail "optimized IR should contain no dead branches or loops" "$optimizedIR"
fi
expect_listing "optimized IR" "$optimizedIR" 'PRINT 20' 'PRINT 2' 'PRINT "optimizer demo"' 'PRINT true'
expect_listing "string escaping in IR" "$(./customlang tests/string_ir_escaping.psk --ir)" \
    'PRINT "line1\n\"quoted\"\\path"'

# ---------------------------------------------------------------------------
# LLVM backend (skipped when built without LLVM)
# ---------------------------------------------------------------------------
if ./customlang tests/llvm_backend.psk --llvm-ir 2>&1 >/dev/null | grep -q "compiled without LLVM"; then
    echo "SKIP: LLVM backend tests (this build was compiled without LLVM)"
else
    llvmOutput="$(./customlang tests/llvm_backend.psk --llvm-ir)" ||
        fail "LLVM backend should compile and optimize the scalar test" "$llvmOutput"
    expect_listing "LLVM IR" "$llvmOutput" 'i32 @main' 'i64 5'
    if grep -q 'unreachable branch' <<<"$llvmOutput"; then
        fail "LLVM O2 pipeline did not remove the dead branch" "$llvmOutput"
    fi
    if ./customlang tests/missing_syntax.psk --llvm-ir >/dev/null 2>&1; then
        fail "unsupported features should be rejected by the LLVM backend"
    fi
fi

# ---------------------------------------------------------------------------
# Slow: the loop guard needs 10,000,000 iterations (a few seconds).
# ---------------------------------------------------------------------------
expect_error tests/infinite_loop.psk 2 "possible infinite loop"

# Every program in tests/ must be used above.
for file in tests/*.psk; do
    grep -Fq -- "$file" "$0" || fail "$file is not used by test.sh"
done

echo "PASS: all $checked checks"
