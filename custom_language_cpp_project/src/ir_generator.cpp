#include "ir_generator.hpp"

std::string IRGenerator::nextTemp() { return "%t" + std::to_string(++temp); }
std::string IRGenerator::nextLabel(const std::string& prefix) { return prefix + std::to_string(++label); }

std::string IRGenerator::expr(const Expr* e) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(e)) {
        if (literal->value.type == ValueType::String) {
            return "\"" + literal->value.s + "\"";
        }
        return literal->value.toString();
    }

    if (auto* variable = dynamic_cast<const VariableExpr*>(e)) {
        return variable->name;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(e)) {
        return member->field + " of " + expr(member->object.get());
    }

    if (auto* list = dynamic_cast<const ListExpr*>(e)) {
        if (list->operation == ListExpr::Operation::Length) return "LENGTH " + list->name;
        if (list->operation == ListExpr::Operation::Top) return "TOP " + list->name;
        return "ITEM " + list->name + "[" + expr(list->index.get()) + "]";
    }

    if (auto* call = dynamic_cast<const CallExpr*>(e)) {
        std::string result = call->name + "(";
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i != 0) result += ", ";
            result += expr(call->arguments[i].get());
        }
        return result + ")";
    }

    if (auto* unary = dynamic_cast<const UnaryExpr*>(e)) {
        const std::string tempName = nextTemp();
        lines.push_back(tempName + " = " + unary->op + " " + expr(unary->expr.get()));
        return tempName;
    }

    auto* binary = dynamic_cast<const BinaryExpr*>(e);
    if (!binary) return "<invalid-expr>";

    const std::string tempName = nextTemp();
    lines.push_back(tempName + " = " + expr(binary->left.get()) + " " +
                   binary->op + " " + expr(binary->right.get()));
    return tempName;
}

void IRGenerator::stmt(const Stmt* s) {
    if (auto* block = dynamic_cast<const BlockStmt*>(s)) {
        this->block(block);
        return;
    }

    if (auto* function = dynamic_cast<const FunctionStmt*>(s)) {
        std::string signature = "FUNCTION " + function->name;
        if (!function->genericTypeParameters.empty()) {
            signature += "<";
            for (std::size_t i = 0; i < function->genericTypeParameters.size(); ++i) {
                if (i != 0) signature += ", ";
                signature += function->genericTypeParameters[i];
            }
            signature += ">";
        }
        lines.push_back(std::move(signature));
        block(function->body.get());
        lines.push_back("END FUNCTION " + function->name);
        return;
    }

    if (auto* klass = dynamic_cast<const ClassStmt*>(s)) {
        lines.push_back(std::string(klass->isAbstract ? "ABSTRACT CLASS " : "CLASS ") + klass->name +
                        (klass->baseName.empty() ? "" : " INHERITS " + klass->baseName));
        for (const auto& field : klass->fields) {
            lines.push_back(std::string(field.isPublic ? "  PUBLIC FIELD " : "  PRIVATE FIELD ") +
                            typeName(field.type) + " " + field.name);
        }
        for (const auto& method : klass->methods) {
            lines.push_back(std::string(method.isAbstract ? "  ABSTRACT METHOD " : "  METHOD ") +
                            klass->name + "." + method.name);
            if (method.body) {
                for (const auto& child : method.body->statements) stmt(child.get());
            }
        }
        if (klass->hasConstructor) {
            lines.push_back("  CONSTRUCTOR " + klass->name);
            if (klass->constructorBody) {
                for (const auto& child : klass->constructorBody->statements) stmt(child.get());
            }
        }
        lines.push_back("END CLASS " + klass->name);
        return;
    }

    if (auto* structure = dynamic_cast<const StructStmt*>(s)) {
        lines.push_back("STRUCT " + structure->name);
        for (const auto& field : structure->fields) {
            lines.push_back("  FIELD " + typeName(field.type) + " " + field.name);
        }
        lines.push_back("END STRUCT " + structure->name);
        return;
    }

    if (auto* enumeration = dynamic_cast<const EnumStmt*>(s)) {
        lines.push_back("ENUM " + enumeration->name);
        for (std::size_t i = 0; i < enumeration->members.size(); ++i) {
            lines.push_back("  " + enumeration->members[i] + " = " + std::to_string(i));
        }
        lines.push_back("END ENUM " + enumeration->name);
        return;
    }

    if (auto* declaration = dynamic_cast<const DeclarationStmt*>(s)) {
        lines.push_back("DECLARE " + typeName(declaration->type) + " " + declaration->name);
        if (declaration->initializer) {
            lines.push_back(declaration->name + " = " + expr(declaration->initializer.get()));
        }
        return;
    }

    if (auto* initialize = dynamic_cast<const InitializeStmt*>(s)) {
        lines.push_back("DECLARE " + typeName(initialize->type) + " " + initialize->name);
        return;
    }

    if (auto* initialize = dynamic_cast<const NamedInitializeStmt*>(s)) {
        lines.push_back(initialize->name + " = DEFAULT " + initialize->typeName);
        return;
    }

    if (auto* assignment = dynamic_cast<const AssignmentStmt*>(s)) {
        lines.push_back(assignment->name + " = " + expr(assignment->expr.get()));
        return;
    }

    if (auto* assignment = dynamic_cast<const MemberAssignmentStmt*>(s)) {
        lines.push_back(assignment->objectName + "." + assignment->field + " = " +
                        expr(assignment->expr.get()));
        return;
    }

    if (auto* instance = dynamic_cast<const InstantiateStmt*>(s)) {
        std::string line = instance->name + " = NEW " + instance->className + "(";
        for (std::size_t i = 0; i < instance->arguments.size(); ++i) {
            if (i != 0) line += ", ";
            line += expr(instance->arguments[i].get());
        }
        lines.push_back(line + ")");
        return;
    }

    if (auto* call = dynamic_cast<const MethodCallStmt*>(s)) {
        std::string line = "CALL " + call->objectName + "." + call->method + "(";
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i != 0) line += ", ";
            line += expr(call->arguments[i].get());
        }
        lines.push_back(line + ")");
        return;
    }

    if (auto* output = dynamic_cast<const OutputStmt*>(s)) {
        lines.push_back("PRINT " + expr(output->expr.get()));
        return;
    }

    if (auto* expression = dynamic_cast<const ExpressionStmt*>(s)) {
        lines.push_back("EVAL " + expr(expression->expression.get()));
        return;
    }

    if (auto* fix = dynamic_cast<const FixStmt*>(s)) {
        lines.push_back("FIX " + fix->name);
        return;
    }

    if (auto* returned = dynamic_cast<const ReturnStmt*>(s)) {
        lines.push_back(returned->expression ? "RETURN " + expr(returned->expression.get()) : "RETURN");
        return;
    }

    if (auto* control = dynamic_cast<const LoopControlStmt*>(s)) {
        lines.push_back(control->isBreak ? "BREAK" : "CONTINUE");
        return;
    }

    if (auto* mutation = dynamic_cast<const ListMutationStmt*>(s)) {
        if (mutation->operation == ListMutationStmt::Operation::Append) {
            lines.push_back("APPEND " + expr(mutation->value.get()) + " TO " + mutation->name);
        } else {
            lines.push_back("REMOVE_TOP " + mutation->name);
        }
        return;
    }

    if (auto* input = dynamic_cast<const InputStmt*>(s)) {
        lines.push_back("READ " + input->name + " PROMPT " + expr(input->prompt.get()));
        return;
    }

    if (auto* compound = dynamic_cast<const AddToStmt*>(s)) {
        const std::string op = compound->op == "add" ? "+"
                             : compound->op == "subtract" ? "-"
                             : compound->op == "multiply" ? "*"
                             : compound->op == "mod" ? "mod" : "/";
        lines.push_back(compound->name + " = " + compound->name + " " +
                        op + " " + expr(compound->amount.get()));
        return;
    }

    if (auto* ifStmt = dynamic_cast<const IfStmt*>(s)) {
        const std::string elseLabel = nextLabel("else");
        const std::string endLabel = nextLabel("endif");
        const std::string condition = expr(ifStmt->condition.get());
        lines.push_back("IF_FALSE " + condition + " GOTO " + elseLabel);
        block(ifStmt->thenBlock.get());
        if (ifStmt->elseBlock) lines.push_back("GOTO " + endLabel);
        lines.push_back(elseLabel + ":");
        if (ifStmt->elseBlock) block(ifStmt->elseBlock.get());
        if (ifStmt->elseBlock) lines.push_back(endLabel + ":");
        return;
    }

    if (auto* check = dynamic_cast<const CheckStmt*>(s)) {
        const std::string endLabel = nextLabel("endcheck");
        const std::string value = expr(check->value.get());
        for (const auto& item : check->cases) {
            const std::string nextLabelName = nextLabel("nextwhen");
            const std::string condition = nextTemp();
            lines.push_back(condition + " = " + value + " == " + expr(item.value.get()));
            lines.push_back("IF_FALSE " + condition + " GOTO " + nextLabelName);
            block(item.body.get());
            lines.push_back("GOTO " + endLabel);
            lines.push_back(nextLabelName + ":");
        }
        if (check->otherwise) block(check->otherwise.get());
        lines.push_back(endLabel + ":");
        return;
    }

    if (auto* whileStmt = dynamic_cast<const WhileStmt*>(s)) {
        const std::string beginLabel = nextLabel("while");
        const std::string endLabel = nextLabel("endwhile");
        lines.push_back(beginLabel + ":");
        const std::string condition = expr(whileStmt->condition.get());
        lines.push_back("IF_FALSE " + condition + " GOTO " + endLabel);
        block(whileStmt->body.get());
        lines.push_back("GOTO " + beginLabel);
        lines.push_back(endLabel + ":");
        return;
    }

    if (auto* repeat = dynamic_cast<const RepeatStmt*>(s)) {
        const std::string counter = nextTemp();
        const std::string beginLabel = nextLabel("repeat");
        const std::string endLabel = nextLabel("endrepeat");
        lines.push_back(counter + " = 0");
        lines.push_back(beginLabel + ":");
        const std::string count = expr(repeat->count.get());
        const std::string condition = nextTemp();
        lines.push_back(condition + " = " + counter + " < " + count);
        lines.push_back("IF_FALSE " + condition + " GOTO " + endLabel);
        block(repeat->body.get());
        lines.push_back(counter + " = " + counter + " + 1");
        lines.push_back("GOTO " + beginLabel);
        lines.push_back(endLabel + ":");
        return;
    }

    if (auto* forStmt = dynamic_cast<const ForStmt*>(s)) {
        stmt(forStmt->init.get());
        const std::string beginLabel = nextLabel("for");
        const std::string endLabel = nextLabel("endfor");
        lines.push_back(beginLabel + ":");
        const std::string condition = expr(forStmt->condition.get());
        lines.push_back("IF_FALSE " + condition + " GOTO " + endLabel);
        block(forStmt->body.get());
        stmt(forStmt->increment.get());
        lines.push_back("GOTO " + beginLabel);
        lines.push_back(endLabel + ":");
        return;
    }

    if (auto* each = dynamic_cast<const ForEachStmt*>(s)) {
        const std::string beginLabel = nextLabel("each");
        const std::string endLabel = nextLabel("endeach");
        lines.push_back(each->name + " = ITERATOR " + expr(each->collection.get()));
        lines.push_back(beginLabel + ":");
        lines.push_back("IF_ITERATOR_END GOTO " + endLabel);
        block(each->body.get());
        lines.push_back("GOTO " + beginLabel);
        lines.push_back(endLabel + ":");
        return;
    }
}

void IRGenerator::block(const BlockStmt* b) {
    for (const auto& statement : b->statements) stmt(statement.get());
}

std::vector<std::string> IRGenerator::generate(const Program& program) {
    lines.clear();
    temp = 0;
    label = 0;
    for (const auto& statement : program) stmt(statement.get());
    return lines;
}
