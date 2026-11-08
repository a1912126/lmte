#pragma once

#include "ast.hpp"
#include <optional>

void optimizeProgram(Program &program);

// Evaluates an expression made only of literals, using the same rules as the
// interpreter (integer division for two ints, no folding on overflow).
// Returns nothing if the expression is not a constant or cannot be folded.
std::optional<Value> evaluateConstantExpression(const Expr *expression);
