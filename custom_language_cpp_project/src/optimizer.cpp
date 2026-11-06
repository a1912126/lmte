#include "optimizer.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace {
bool checkedAdd(long long left, long long right, long long& result) {
    if ((right > 0 && left > std::numeric_limits<long long>::max() - right) ||
        (right < 0 && left < std::numeric_limits<long long>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedSubtract(long long left, long long right, long long& result) {
    if ((right < 0 && left > std::numeric_limits<long long>::max() + right) ||
        (right > 0 && left < std::numeric_limits<long long>::min() + right)) {
        return false;
    }
    result = left - right;
    return true;
}

bool checkedMultiply(long long left, long long right, long long& result) {
    if (left == 0 || right == 0) {
        result = 0;
        return true;
    }
    if ((left == -1 && right == std::numeric_limits<long long>::min()) ||
        (right == -1 && left == std::numeric_limits<long long>::min())) {
        return false;
    }
    if (left > 0) {
        if ((right > 0 && left > std::numeric_limits<long long>::max() / right) ||
            (right < 0 && right < std::numeric_limits<long long>::min() / left)) {
            return false;
        }
    } else if ((right > 0 && left < std::numeric_limits<long long>::min() / right) ||
               (right < 0 && left < std::numeric_limits<long long>::max() / right)) {
        return false;
    }
    result = left * right;
    return true;
}

std::optional<Value> evaluateConstant(const Expr* expression) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(expression)) {
        if (literal->value.type == ValueType::Int ||
            literal->value.type == ValueType::Float ||
            literal->value.type == ValueType::Bool ||
            literal->value.type == ValueType::String) {
            return literal->value;
        }
        return std::nullopt;
    }

    if (auto* unary = dynamic_cast<const UnaryExpr*>(expression)) {
        auto value = evaluateConstant(unary->expr.get());
        if (!value) return std::nullopt;
        if (unary->op == "+" && (value->type == ValueType::Int ||
                                  value->type == ValueType::Float)) {
            return value;
        }
        if (unary->op == "-" && value->type == ValueType::Int &&
            value->i != std::numeric_limits<long long>::min()) {
            return Value::makeInt(-value->i);
        }
        if (unary->op == "-" && value->type == ValueType::Float) {
            return Value::makeFloat(-value->f);
        }
        if (unary->op == "not" && value->type == ValueType::Bool) {
            return Value::makeBool(!value->b);
        }
        return std::nullopt;
    }

    auto* binary = dynamic_cast<const BinaryExpr*>(expression);
    if (!binary) return std::nullopt;
    auto left = evaluateConstant(binary->left.get());
    if (!left) return std::nullopt;
    if (binary->op == "and" && left->type == ValueType::Bool && !left->b) {
        return Value::makeBool(false);
    }
    if (binary->op == "or" && left->type == ValueType::Bool && left->b) {
        return Value::makeBool(true);
    }
    auto right = evaluateConstant(binary->right.get());
    if (!right) return std::nullopt;

    if (binary->op == "+" && left->type == ValueType::String &&
        right->type == ValueType::String) {
        return Value::makeString(left->s + right->s);
    }
    if (binary->op == "and" || binary->op == "or") {
        if (left->type != ValueType::Bool || right->type != ValueType::Bool) {
            return std::nullopt;
        }
        return Value::makeBool(binary->op == "and" ? left->b && right->b
                                                    : left->b || right->b);
    }

    const bool leftNumeric = left->type == ValueType::Int || left->type == ValueType::Float;
    const bool rightNumeric = right->type == ValueType::Int || right->type == ValueType::Float;
    if (binary->op == "==" || binary->op == "!=") {
        bool equal = false;
        if (leftNumeric && rightNumeric) {
            const double a = left->type == ValueType::Int ? static_cast<double>(left->i) : left->f;
            const double b = right->type == ValueType::Int ? static_cast<double>(right->i) : right->f;
            equal = left->type == right->type
                        ? (left->type == ValueType::Int ? left->i == right->i
                                                       : std::fabs(left->f - right->f) < 1e-12)
                        : std::fabs(a - b) < 1e-12;
        } else if (left->type == right->type) {
            if (left->type == ValueType::Bool) equal = left->b == right->b;
            else if (left->type == ValueType::String) equal = left->s == right->s;
            else return std::nullopt;
        } else {
            return std::nullopt;
        }
        return Value::makeBool(binary->op == "==" ? equal : !equal);
    }

    if ((binary->op == "<" || binary->op == "<=" ||
         binary->op == ">" || binary->op == ">=") &&
        left->type == ValueType::String && right->type == ValueType::String) {
        bool result = false;
        if (binary->op == "<") result = left->s < right->s;
        else if (binary->op == "<=") result = left->s <= right->s;
        else if (binary->op == ">") result = left->s > right->s;
        else result = left->s >= right->s;
        return Value::makeBool(result);
    }
    if (!leftNumeric || !rightNumeric) return std::nullopt;

    const bool bothIntegers = left->type == ValueType::Int && right->type == ValueType::Int;
    const double a = left->type == ValueType::Int ? static_cast<double>(left->i) : left->f;
    const double b = right->type == ValueType::Int ? static_cast<double>(right->i) : right->f;
    if (binary->op == "<" || binary->op == "<=" ||
        binary->op == ">" || binary->op == ">=") {
        bool result = false;
        if (bothIntegers) {
            // Compare integers exactly: doubles cannot hold every value above 2^53.
            if (binary->op == "<") result = left->i < right->i;
            else if (binary->op == "<=") result = left->i <= right->i;
            else if (binary->op == ">") result = left->i > right->i;
            else result = left->i >= right->i;
            return Value::makeBool(result);
        }
        if (binary->op == "<") result = a < b;
        else if (binary->op == "<=") result = a <= b;
        else if (binary->op == ">") result = a > b;
        else result = a >= b;
        return Value::makeBool(result);
    }

    if (bothIntegers) {
        long long result = 0;
        if (binary->op == "+" && checkedAdd(left->i, right->i, result)) {
            return Value::makeInt(result);
        }
        if (binary->op == "-" && checkedSubtract(left->i, right->i, result)) {
            return Value::makeInt(result);
        }
        if (binary->op == "*" && checkedMultiply(left->i, right->i, result)) {
            return Value::makeInt(result);
        }
        if (binary->op == "/" && right->i != 0 &&
            !(left->i == std::numeric_limits<long long>::min() && right->i == -1)) {
            return Value::makeInt(left->i / right->i);
        }
        if (binary->op == "mod" && right->i != 0 &&
            !(left->i == std::numeric_limits<long long>::min() && right->i == -1)) {
            return Value::makeInt(left->i % right->i);
        }
        return std::nullopt;
    }

    if (binary->op == "+") return Value::makeFloat(a + b);
    if (binary->op == "-") return Value::makeFloat(a - b);
    if (binary->op == "*") return Value::makeFloat(a * b);
    if (binary->op == "/" && std::fabs(b) >= 1e-15) return Value::makeFloat(a / b);
    if (binary->op == "mod" && std::fabs(b) >= 1e-15) {
        return Value::makeFloat(std::fmod(a, b));
    }
    return std::nullopt;
}

void optimizeExpression(ExprPtr& expression);
void optimizeStatement(StmtPtr& statement);

void optimizeBlock(BlockStmt* block) {
    if (!block) return;
    for (auto& statement : block->statements) optimizeStatement(statement);
}

void optimizeExpression(ExprPtr& expression) {
    if (!expression) return;
    if (auto* binary = dynamic_cast<BinaryExpr*>(expression.get())) {
        optimizeExpression(binary->left);
        optimizeExpression(binary->right);
    } else if (auto* unary = dynamic_cast<UnaryExpr*>(expression.get())) {
        optimizeExpression(unary->expr);
    } else if (auto* member = dynamic_cast<MemberExpr*>(expression.get())) {
        optimizeExpression(member->object);
    } else if (auto* call = dynamic_cast<CallExpr*>(expression.get())) {
        for (auto& argument : call->arguments) optimizeExpression(argument);
    } else if (auto* list = dynamic_cast<ListExpr*>(expression.get())) {
        optimizeExpression(list->index);
    }

    auto value = evaluateConstant(expression.get());
    if (value) expression = std::make_unique<LiteralExpr>(std::move(*value));
}

void optimizeStatement(StmtPtr& statement) {
    if (!statement) return;
    if (auto* function = dynamic_cast<FunctionStmt*>(statement.get())) {
        optimizeBlock(function->body.get());
    } else if (auto* klass = dynamic_cast<ClassStmt*>(statement.get())) {
        optimizeBlock(klass->constructorBody.get());
        for (auto& method : klass->methods) optimizeBlock(method.body.get());
    } else if (auto* declaration = dynamic_cast<DeclarationStmt*>(statement.get())) {
        optimizeExpression(declaration->initializer);
    } else if (auto* assignment = dynamic_cast<AssignmentStmt*>(statement.get())) {
        optimizeExpression(assignment->expr);
    } else if (auto* assignment = dynamic_cast<MemberAssignmentStmt*>(statement.get())) {
        optimizeExpression(assignment->expr);
    } else if (auto* output = dynamic_cast<OutputStmt*>(statement.get())) {
        optimizeExpression(output->expr);
    } else if (auto* expression = dynamic_cast<ExpressionStmt*>(statement.get())) {
        optimizeExpression(expression->expression);
    } else if (auto* input = dynamic_cast<InputStmt*>(statement.get())) {
        optimizeExpression(input->prompt);
    } else if (auto* compound = dynamic_cast<AddToStmt*>(statement.get())) {
        optimizeExpression(compound->amount);
    } else if (auto* mutation = dynamic_cast<ListMutationStmt*>(statement.get())) {
        optimizeExpression(mutation->value);
    } else if (auto* instance = dynamic_cast<InstantiateStmt*>(statement.get())) {
        for (auto& argument : instance->arguments) optimizeExpression(argument);
    } else if (auto* call = dynamic_cast<MethodCallStmt*>(statement.get())) {
        for (auto& argument : call->arguments) optimizeExpression(argument);
    } else if (auto* superCall = dynamic_cast<SuperConstructorCallStmt*>(statement.get())) {
        for (auto& argument : superCall->arguments) optimizeExpression(argument);
    } else if (auto* returned = dynamic_cast<ReturnStmt*>(statement.get())) {
        optimizeExpression(returned->expression);
    } else if (auto* branch = dynamic_cast<IfStmt*>(statement.get())) {
        optimizeExpression(branch->condition);
        optimizeBlock(branch->thenBlock.get());
        optimizeBlock(branch->elseBlock.get());
        if (auto* literal = dynamic_cast<LiteralExpr*>(branch->condition.get());
            literal && literal->value.type == ValueType::Bool) {
            statement = literal->value.b
                            ? std::unique_ptr<Stmt>(std::move(branch->thenBlock))
                            : std::unique_ptr<Stmt>(std::move(branch->elseBlock));
            if (!statement) statement = std::make_unique<BlockStmt>();
        }
    } else if (auto* loop = dynamic_cast<WhileStmt*>(statement.get())) {
        optimizeExpression(loop->condition);
        optimizeBlock(loop->body.get());
        if (auto* literal = dynamic_cast<LiteralExpr*>(loop->condition.get());
            literal && literal->value.type == ValueType::Bool && !literal->value.b) {
            statement = std::make_unique<BlockStmt>();
        }
    } else if (auto* loop = dynamic_cast<RepeatStmt*>(statement.get())) {
        optimizeExpression(loop->count);
        optimizeBlock(loop->body.get());
    } else if (auto* loop = dynamic_cast<ForStmt*>(statement.get())) {
        optimizeStatement(loop->init);
        optimizeExpression(loop->condition);
        optimizeStatement(loop->increment);
        optimizeBlock(loop->body.get());
    } else if (auto* loop = dynamic_cast<ForEachStmt*>(statement.get())) {
        optimizeExpression(loop->collection);
        optimizeBlock(loop->body.get());
    } else if (auto* check = dynamic_cast<CheckStmt*>(statement.get())) {
        optimizeExpression(check->value);
        for (auto& item : check->cases) {
            optimizeExpression(item.value);
            optimizeBlock(item.body.get());
        }
        optimizeBlock(check->otherwise.get());
    } else if (auto* block = dynamic_cast<BlockStmt*>(statement.get())) {
        optimizeBlock(block);
    }
}
}

std::optional<Value> evaluateConstantExpression(const Expr* expression) {
    return evaluateConstant(expression);
}

void optimizeProgram(Program& program) {
    for (auto& statement : program) optimizeStatement(statement);
}
