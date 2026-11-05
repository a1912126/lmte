#!/usr/bin/env bash
set -u

make -s clean
make -s

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

echo "PASS: all tests"
