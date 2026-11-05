#include "semantic.hpp"
#include "common.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
std::optional<long double> constantNumber(const Expr* expression) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(expression)) {
        if (literal->value.type == ValueType::Int) return literal->value.i;
        if (literal->value.type == ValueType::Float) return literal->value.f;
        return std::nullopt;
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(expression)) {
        auto value = constantNumber(unary->expr.get());
        if (!value) return std::nullopt;
        if (unary->op == "+") return value;
        if (unary->op == "-") return -*value;
        return std::nullopt;
    }
    auto* binary = dynamic_cast<const BinaryExpr*>(expression);
    if (!binary) return std::nullopt;
    auto left = constantNumber(binary->left.get());
    auto right = constantNumber(binary->right.get());
    if (!left || !right) return std::nullopt;
    if (binary->op == "+") return *left + *right;
    if (binary->op == "-") return *left - *right;
    if (binary->op == "*") return *left * *right;
    if (binary->op == "/" && *right != 0.0L) return *left / *right;
    if (binary->op == "mod" && *right != 0.0L) return std::fmod(*left, *right);
    return std::nullopt;
}
}

[[noreturn]] void SemanticAnalyzer::error(const std::string& message) const {
    throw CompileError("Semantic error: " + message);
}

bool SemanticAnalyzer::isNumeric(ValueType type) const {
    return type == ValueType::Int || type == ValueType::Float;
}

bool SemanticAnalyzer::assignable(ValueType target, ValueType source) const {
    return target == source || target == ValueType::Any || source == ValueType::Any ||
           (target == ValueType::Float && source == ValueType::Int);
}

const ClassField* SemanticAnalyzer::findField(
    const std::string& className, const std::string& field, std::string* declaringClass) const {
    auto classIt = classes.find(className);
    if (classIt == classes.end()) return nullptr;
    for (const auto& candidate : classIt->second->fields) {
        if (candidate.name == field) {
            if (declaringClass) *declaringClass = className;
            return &candidate;
        }
    }
    if (!classIt->second->baseName.empty()) {
        return findField(classIt->second->baseName, field, declaringClass);
    }
    return nullptr;
}

const ClassField* SemanticAnalyzer::findObjectField(
    const std::string& typeName, const std::string& field, std::string* declaringClass) const {
    if (classes.count(typeName)) return findField(typeName, field, declaringClass);
    auto structure = structs.find(typeName);
    if (structure == structs.end()) return nullptr;
    for (const auto& candidate : structure->second->fields) {
        if (candidate.name == field) {
            if (declaringClass) *declaringClass = typeName;
            return &candidate;
        }
    }
    return nullptr;
}

const ClassMethod* SemanticAnalyzer::findMethod(
    const std::string& className, const std::string& method, std::string* declaringClass) const {
    auto classIt = classes.find(className);
    if (classIt == classes.end()) return nullptr;
    for (const auto& candidate : classIt->second->methods) {
        if (candidate.name == method) {
            if (declaringClass) *declaringClass = className;
            return &candidate;
        }
    }
    if (!classIt->second->baseName.empty()) {
        return findMethod(classIt->second->baseName, method, declaringClass);
    }
    return nullptr;
}

bool SemanticAnalyzer::derivesFrom(
    const std::string& className, const std::string& baseName) const {
    if (className == baseName) return true;
    auto it = classes.find(className);
    return it != classes.end() && !it->second->baseName.empty() &&
           derivesFrom(it->second->baseName, baseName);
}

std::string SemanticAnalyzer::enumTypeOf(const Expr* expr) const {
    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto declaredEnum = declaredEnums.find(variable->name);
        if (declaredEnum != declaredEnums.end()) return declaredEnum->second;
        if (declared.count(variable->name)) return {};
        for (const auto& [enumName, definition] : enums) {
            if (std::find(definition->members.begin(), definition->members.end(),
                          variable->name) != definition->members.end()) {
                return enumName;
            }
        }
    }
    if (auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
        if (literal->value.type == ValueType::Enum) return literal->value.enumType;
    }
    return {};
}

ValueType SemanticAnalyzer::exprType(const Expr* expr) const {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(expr)) return literal->value.type;
    if (auto* list = dynamic_cast<const ListExpr*>(expr)) {
        const auto variable = declared.find(list->name);
        if (variable == declared.end() || variable->second != ValueType::List) {
            error("'" + list->name + "' is not a declared list");
        }
        if (list->operation == ListExpr::Operation::Length) return ValueType::Int;
        if (list->operation == ListExpr::Operation::Index &&
            exprType(list->index.get()) != ValueType::Int) {
            error("list index must be an integer");
        }
        return ValueType::Any;
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const auto function = functions.find(call->name);
        if (function == functions.end()) error("function '" + call->name + "' is not declared");
        if (function->second->parameters.size() != call->arguments.size()) {
            error("wrong number of arguments for function '" + call->name + "'");
        }
        std::unordered_map<std::string, ValueType> genericBindings;
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            const Parameter& parameter = function->second->parameters[i];
            const ValueType argumentType = exprType(call->arguments[i].get());
            if (!parameter.genericType.empty()) {
                auto binding = genericBindings.find(parameter.genericType);
                if (binding == genericBindings.end() || binding->second == ValueType::Any) {
                    genericBindings[parameter.genericType] = argumentType;
                } else if (argumentType != ValueType::Any && binding->second != argumentType) {
                    error("generic type '" + parameter.genericType +
                          "' has conflicting argument types in call to '" + call->name + "'");
                }
            } else if (!assignable(parameter.type, argumentType)) {
                error("argument type mismatch for function '" + call->name + "'");
            }
        }
        if (!function->second->genericReturnType.empty()) {
            auto binding = genericBindings.find(function->second->genericReturnType);
            if (binding == genericBindings.end()) {
                error("cannot infer generic return type '" +
                      function->second->genericReturnType + "' for function '" + call->name + "'");
            }
            return binding->second;
        }
        return function->second->returnType;
    }
    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto it = declared.find(variable->name);
        if (it != declared.end()) return it->second;
        std::string owner;
        const ClassField* field = currentClass.empty()
                                      ? nullptr
                                      : findField(currentClass, variable->name, &owner);
        if (field && (field->isPublic || owner == currentClass)) return field->type;
        if (!enumTypeOf(variable).empty()) return ValueType::Enum;
        error("variable or field '" + variable->name + "' used before declaration");
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        auto* objectExpr = dynamic_cast<const VariableExpr*>(member->object.get());
        if (!objectExpr) error("field access requires an object variable");
        auto classIt = declaredClasses.find(objectExpr->name);
        if (classIt == declaredClasses.end()) error("'" + objectExpr->name + "' is not a class instance");
        std::string owner;
        const ClassField* field = findObjectField(classIt->second, member->field, &owner);
        if (!field) error("type '" + classIt->second + "' has no field '" + member->field + "'");
        if (!field->isPublic && owner != currentClass) {
            error("field '" + member->field + "' is private");
        }
        return field->type;
    }
    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        ValueType type = exprType(unary->expr.get());
        if (unary->op == "not") {
            if (type != ValueType::Bool && type != ValueType::Any) error("'not' requires a boolean expression");
            return ValueType::Bool;
        }
        if (!isNumeric(type)) error("unary '" + unary->op + "' requires a numeric expression");
        return type;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        ValueType left = exprType(binary->left.get());
        ValueType right = exprType(binary->right.get());
        if (binary->op == "and" || binary->op == "or") {
            if ((left != ValueType::Bool && left != ValueType::Any) ||
                (right != ValueType::Bool && right != ValueType::Any)) {
                error("logical operators require boolean operands");
            }
            return ValueType::Bool;
        }
        if (binary->op == "+" || binary->op == "-" ||
            binary->op == "*" || binary->op == "/" || binary->op == "mod") {
            if (binary->op == "+" && left == ValueType::String && right == ValueType::String) {
                return ValueType::String;
            }
            if ((!isNumeric(left) && left != ValueType::Any) ||
                (!isNumeric(right) && right != ValueType::Any)) {
                error("operator '" + binary->op + "' requires numeric operands");
            }
            if (binary->op == "/" || binary->op == "mod") {
                const auto divisor = constantNumber(binary->right.get());
                if (divisor && std::fabs(*divisor) < 1e-15L) {
                    error(binary->op == "/" ? "division by constant zero"
                                               : "modulo by constant zero");
                }
            }
            if (left == ValueType::Any || right == ValueType::Any) return ValueType::Any;
            return left == ValueType::Float || right == ValueType::Float
                       ? ValueType::Float
                       : ValueType::Int;
        }
        if (binary->op == "==" || binary->op == "!=") {
            if ((isNumeric(left) && isNumeric(right)) || left == right ||
                left == ValueType::Any || right == ValueType::Any) {
                if (left != ValueType::Enum || enumTypeOf(binary->left.get()) ==
                                                   enumTypeOf(binary->right.get())) {
                    return ValueType::Bool;
                }
            }
            error("cannot compare " + typeName(left) + " with " + typeName(right));
        }
        if (binary->op == "<" || binary->op == "<=" ||
            binary->op == ">" || binary->op == ">=") {
            if ((isNumeric(left) && isNumeric(right)) ||
                (left == ValueType::String && right == ValueType::String) ||
                left == ValueType::Any || right == ValueType::Any) return ValueType::Bool;
            error("comparison requires numeric operands or two strings");
        }
        error("unknown operator '" + binary->op + "'");
    }
    error("invalid expression");
}

std::string SemanticAnalyzer::genericExprType(const Expr* expr) const {
    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto it = declaredGenericTypes.find(variable->name);
        return it == declaredGenericTypes.end() ? "" : it->second;
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        auto function = functions.find(call->name);
        if (function == functions.end() || function->second->genericReturnType.empty()) return "";
        const std::string& genericReturn = function->second->genericReturnType;
        for (std::size_t i = 0; i < function->second->parameters.size(); ++i) {
            if (function->second->parameters[i].genericType == genericReturn &&
                i < call->arguments.size()) {
                return genericExprType(call->arguments[i].get());
            }
        }
    }
    return "";
}

void SemanticAnalyzer::validateAssignmentType(
    ValueType target, const Expr* expr, const std::string& name) const {
    const ValueType source = exprType(expr);
    if (target == ValueType::String) {
        const auto* literal = dynamic_cast<const LiteralExpr*>(expr);
        const auto* variable = dynamic_cast<const VariableExpr*>(expr);
        if ((literal && literal->value.type == ValueType::String) ||
            (variable && source == ValueType::String) || source == ValueType::Any) return;
        error("string variable '" + name +
              "' must be assigned a quoted string literal or another string variable");
    }
    if (target == ValueType::Enum && enumTypeOf(expr).empty()) {
        error("enum variable '" + name + "' must be assigned one of its named members");
    }
    if (!assignable(target, source)) {
        error("cannot assign " + typeName(source) + " to " + typeName(target) +
              " variable or field '" + name + "'");
    }
}

ValueType SemanticAnalyzer::inputTargetType(const std::string& name) const {
    auto variable = declared.find(name);
    if (variable != declared.end()) return variable->second;

    std::string owner;
    const ClassField* field = currentClass.empty() ? nullptr : findField(currentClass, name, &owner);
    if (field && (field->isPublic || owner == currentClass)) return field->type;
    error("ask target '" + name + "' must be a declared variable");
}

void SemanticAnalyzer::validateClass(const ClassStmt& klass) {
    if (!klass.baseName.empty() && classes.count(klass.baseName) == 0) {
        error("base class '" + klass.baseName + "' is not declared");
    }
    if (derivesFrom(klass.baseName, klass.name)) {
        error("inheritance cycle involving class '" + klass.name + "'");
    }

    std::unordered_map<std::string, bool> fieldNames;
    for (const auto& field : klass.fields) {
        if (!fieldNames.emplace(field.name, true).second ||
            (!klass.baseName.empty() && findField(klass.baseName, field.name))) {
            error("field '" + field.name + "' already exists in class hierarchy");
        }
    }
    std::unordered_map<std::string, bool> methodNames;
    for (const auto& method : klass.methods) {
        if (!methodNames.emplace(method.name, true).second) {
            error("method '" + method.name + "' is declared more than once");
        }
        if (!klass.baseName.empty()) {
            std::string baseOwner;
            const ClassMethod* baseMethod = findMethod(klass.baseName, method.name, &baseOwner);
            if (baseMethod &&
                (baseMethod->returnType != method.returnType ||
                 baseMethod->parameters.size() != method.parameters.size() ||
                 baseMethod->isPublic != method.isPublic)) {
                error("override of '" + method.name + "' must preserve visibility and signature");
            }
            if (baseMethod) {
                for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                    if (baseMethod->parameters[i].type != method.parameters[i].type) {
                        error("override of '" + method.name + "' must preserve parameter types");
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, bool> requiredMethods;
    for (const auto& [className, definition] : classes) {
        (void)className;
        for (const auto& method : definition->methods) {
            if (method.isAbstract) requiredMethods[method.name] = true;
        }
    }
    for (const auto& [methodName, unused] : requiredMethods) {
        (void)unused;
        const ClassMethod* method = findMethod(klass.name, methodName);
        if (!klass.isAbstract && method && method->isAbstract) {
            error("concrete class '" + klass.name +
                  "' must implement abstract method '" + methodName + "'");
        }
    }

    const auto savedDeclared = declared;
    const auto savedClasses = declaredClasses;
    const auto savedEnums = declaredEnums;
    const auto savedFixedVariables = fixedVariables;
    const std::string savedCurrentClass = currentClass;
    const ValueType savedReturnType = currentReturnType;
    const bool savedInsideFunction = insideFunction;
    const bool savedInsideConstructor = insideConstructor;
    const bool savedSuperCallAllowed = superCallAllowed;
    currentClass = klass.name;
    auto validateBody = [&](const std::vector<Parameter>& parameters, BlockStmt* body,
                            bool isConstructor) {
        declared.clear();
        declaredClasses.clear();
        declaredEnums.clear();
        fixedVariables.clear();
        std::unordered_set<std::string> parameterNames;
        for (const auto& parameter : parameters) {
            if (!parameterNames.insert(parameter.name).second) {
                error("parameter '" + parameter.name + "' is declared more than once");
            }
            declared[parameter.name] = parameter.type;
        }
        currentReturnType = ValueType::Void;
        insideFunction = true;
        insideConstructor = isConstructor;
        superCallAllowed = isConstructor;
        if (body) {
            for (auto& stmt : body->statements) statement(stmt.get());
        }
    };
    if (klass.hasConstructor) {
        validateBody(klass.constructorParameters, klass.constructorBody.get(), true);
    }
    for (const auto& method : klass.methods) {
        if (!method.isAbstract) validateBody(method.parameters, method.body.get(), false);
    }
    declared = savedDeclared;
    declaredClasses = savedClasses;
    declaredEnums = savedEnums;
    fixedVariables = savedFixedVariables;
    currentClass = savedCurrentClass;
    currentReturnType = savedReturnType;
    insideFunction = savedInsideFunction;
    insideConstructor = savedInsideConstructor;
    superCallAllowed = savedSuperCallAllowed;
}

void SemanticAnalyzer::statement(Stmt* stmt) {
    if (auto* superCall = dynamic_cast<SuperConstructorCallStmt*>(stmt)) {
        if (!insideConstructor) error("'super()' can only be used inside a constructor");
        if (!superCallAllowed) {
            error("'super()' must be the first statement in a constructor");
        }
        superCallAllowed = false;
        auto current = classes.find(currentClass);
        if (current == classes.end() || current->second->baseName.empty()) {
            error("'super()' requires a base class");
        }
        const ClassStmt* constructorClass = classes.at(current->second->baseName);
        while (constructorClass && !constructorClass->hasConstructor &&
               !constructorClass->baseName.empty()) {
            constructorClass = classes.at(constructorClass->baseName);
        }
        const std::vector<Parameter>* expected = nullptr;
        if (constructorClass && constructorClass->hasConstructor) {
            expected = &constructorClass->constructorParameters;
        }
        const std::size_t expectedCount = expected ? expected->size() : 0;
        if (superCall->arguments.size() != expectedCount) {
            error("wrong number of arguments for base constructor");
        }
        for (std::size_t i = 0; i < expectedCount; ++i) {
            if (!assignable((*expected)[i].type, exprType(superCall->arguments[i].get()))) {
                error("base constructor argument type mismatch for '" + (*expected)[i].name + "'");
            }
        }
        return;
    }
    if (insideConstructor && superCallAllowed) superCallAllowed = false;
    if (dynamic_cast<ClassStmt*>(stmt) || dynamic_cast<StructStmt*>(stmt) ||
        dynamic_cast<EnumStmt*>(stmt)) return;
    if (auto* declaration = dynamic_cast<DeclarationStmt*>(stmt)) {
        if (declared.count(declaration->name)) error("variable '" + declaration->name + "' already declared");
        declared[declaration->name] = declaration->type;
        if (declaration->initializer) {
            validateAssignmentType(declaration->type, declaration->initializer.get(), declaration->name);
        }
        return;
    }
    if (auto* initialize = dynamic_cast<InitializeStmt*>(stmt)) {
        if (declared.count(initialize->name)) error("variable '" + initialize->name + "' already declared");
        declared[initialize->name] = initialize->type;
        return;
    }
    if (auto* fix = dynamic_cast<FixStmt*>(stmt)) {
        if (!declared.count(fix->name)) error("cannot fix undeclared variable '" + fix->name + "'");
        if (!fixedVariables.insert(fix->name).second) error("variable '" + fix->name + "' is already fixed");
        return;
    }
    if (dynamic_cast<FunctionStmt*>(stmt)) return;
    if (auto* returned = dynamic_cast<ReturnStmt*>(stmt)) {
        if (!insideFunction) error("'return' can only be used inside a function");
        if (currentReturnType == ValueType::Void) {
            if (returned->expression) error("nothing-returning function cannot return a value");
        } else {
            if (!returned->expression) error("function must return a value");
            if (!currentGenericReturnType.empty() &&
                genericExprType(returned->expression.get()) != currentGenericReturnType) {
                error("return value must have generic type '" + currentGenericReturnType + "'");
            }
            validateAssignmentType(currentReturnType, returned->expression.get(), "return value");
        }
        return;
    }
    if (auto* control = dynamic_cast<LoopControlStmt*>(stmt)) {
        if (loopDepth == 0) error("'stop' and 'skip' can only be used inside a loop");
        (void)control;
        return;
    }
    if (auto* initialize = dynamic_cast<NamedInitializeStmt*>(stmt)) {
        if (declared.count(initialize->name)) {
            error("variable '" + initialize->name + "' already declared");
        }
        auto classIt = classes.find(initialize->typeName);
        if (classIt != classes.end()) {
            const ClassStmt* constructorClass = classIt->second;
            if (constructorClass->isAbstract) {
                error("cannot instantiate abstract class '" + initialize->typeName + "'");
            }
            while (constructorClass && !constructorClass->hasConstructor &&
                   !constructorClass->baseName.empty()) {
                constructorClass = classes.at(constructorClass->baseName);
            }
            if (constructorClass && constructorClass->hasConstructor) {
                if (!constructorClass->constructorPublic) error("class constructor is private");
                if (!constructorClass->constructorParameters.empty()) {
                    error("class '" + initialize->typeName + "' requires constructor arguments");
                }
            }
            declared[initialize->name] = ValueType::Object;
            declaredClasses[initialize->name] = initialize->typeName;
            return;
        }
        if (structs.count(initialize->typeName)) {
            declared[initialize->name] = ValueType::Object;
            declaredClasses[initialize->name] = initialize->typeName;
            return;
        }
        auto enumIt = enums.find(initialize->typeName);
        if (enumIt != enums.end()) {
            declared[initialize->name] = ValueType::Enum;
            declaredEnums[initialize->name] = initialize->typeName;
            return;
        }
        error("type '" + initialize->typeName + "' is not declared");
    }
    if (auto* instance = dynamic_cast<InstantiateStmt*>(stmt)) {
        if (declared.count(instance->name)) error("variable '" + instance->name + "' already declared");
        auto classIt = classes.find(instance->className);
        if (classIt == classes.end()) error("class '" + instance->className + "' is not declared");
        if (classIt->second->isAbstract) error("cannot instantiate abstract class '" + instance->className + "'");
        std::vector<Parameter> expected;
        const ClassStmt* constructorClass = classIt->second;
        while (constructorClass && !constructorClass->hasConstructor &&
               !constructorClass->baseName.empty()) {
            constructorClass = classes.at(constructorClass->baseName);
        }
        if (constructorClass && constructorClass->hasConstructor) {
            if (!constructorClass->constructorPublic) {
                error("class constructor is private");
            }
            expected = constructorClass->constructorParameters;
        }
        if (expected.size() != instance->arguments.size()) {
            error("wrong number of constructor arguments for class '" + instance->className + "'");
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (!assignable(expected[i].type, exprType(instance->arguments[i].get()))) {
                error("constructor argument type mismatch for '" + expected[i].name + "'");
            }
        }
        declared[instance->name] = ValueType::Object;
        declaredClasses[instance->name] = instance->className;
        return;
    }
    if (auto* assignment = dynamic_cast<AssignmentStmt*>(stmt)) {
        auto it = declared.find(assignment->name);
        if (it != declared.end()) {
            if (fixedVariables.count(assignment->name)) {
                error("cannot assign to fixed variable '" + assignment->name + "'");
            }
            auto genericType = declaredGenericTypes.find(assignment->name);
            if (genericType != declaredGenericTypes.end() &&
                genericExprType(assignment->expr.get()) != genericType->second) {
                error("assignment to generic parameter '" + assignment->name +
                      "' must preserve type '" + genericType->second + "'");
            }
            if (it->second == ValueType::Enum &&
                enumTypeOf(assignment->expr.get()) != declaredEnums[assignment->name]) {
                error("enum assignment for '" + assignment->name + "' has a different enum type");
            }
            validateAssignmentType(it->second, assignment->expr.get(), assignment->name);
            return;
        }
        std::string owner;
        const ClassField* field = currentClass.empty()
                                      ? nullptr
                                      : findField(currentClass, assignment->name, &owner);
        if (!field || (!field->isPublic && owner != currentClass)) {
            error("variable or field '" + assignment->name + "' is not declared or accessible");
        }
        validateAssignmentType(field->type, assignment->expr.get(), assignment->name);
        return;
    }
    if (auto* memberAssignment = dynamic_cast<MemberAssignmentStmt*>(stmt)) {
        auto object = declaredClasses.find(memberAssignment->objectName);
        if (object == declaredClasses.end()) error("'" + memberAssignment->objectName + "' is not a class instance");
        std::string owner;
        const ClassField* field = findObjectField(object->second, memberAssignment->field, &owner);
        if (!field) error("type '" + object->second + "' has no field '" + memberAssignment->field + "'");
        if (!field->isPublic && owner != currentClass) error("field '" + memberAssignment->field + "' is private");
        validateAssignmentType(field->type, memberAssignment->expr.get(), memberAssignment->field);
        return;
    }
    if (auto* call = dynamic_cast<MethodCallStmt*>(stmt)) {
        auto object = declaredClasses.find(call->objectName);
        if (object == declaredClasses.end()) error("'" + call->objectName + "' is not a class instance");
        std::string owner;
        const ClassMethod* method = findMethod(object->second, call->method, &owner);
        if (!method) error("class '" + object->second + "' has no method '" + call->method + "'");
        if (!method->isPublic && owner != currentClass) error("method '" + call->method + "' is private");
        if (method->parameters.size() != call->arguments.size()) {
            error("wrong number of arguments for method '" + call->method + "'");
        }
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            if (!assignable(method->parameters[i].type, exprType(call->arguments[i].get()))) {
                error("argument type mismatch for method '" + call->method + "'");
            }
        }
        return;
    }
    if (auto* output = dynamic_cast<OutputStmt*>(stmt)) {
        if (exprType(output->expr.get()) == ValueType::Void) {
            error("cannot output the result of a nothing-returning function");
        }
        return;
    }
    if (auto* expression = dynamic_cast<ExpressionStmt*>(stmt)) {
        exprType(expression->expression.get());
        return;
    }
    if (auto* input = dynamic_cast<InputStmt*>(stmt)) {
        if (fixedVariables.count(input->name)) {
            error("cannot read input into fixed variable '" + input->name + "'");
        }
        if (exprType(input->prompt.get()) != ValueType::String) {
            error("ask prompt must be a string expression");
        }
        const ValueType target = inputTargetType(input->name);
        if (target != ValueType::Int && target != ValueType::Float &&
            target != ValueType::String && target != ValueType::Bool) {
            error("ask target '" + input->name + "' must have type integer, float, string, or bool");
        }
        return;
    }
    if (auto* compound = dynamic_cast<AddToStmt*>(stmt)) {
        if (fixedVariables.count(compound->name)) error("cannot modify fixed variable '" + compound->name + "'");
        auto it = declared.find(compound->name);
        ValueType target;
        if (it != declared.end()) target = it->second;
        else {
            std::string owner;
            const ClassField* field = currentClass.empty()
                                          ? nullptr
                                          : findField(currentClass, compound->name, &owner);
            if (!field || (!field->isPublic && owner != currentClass)) {
                error("variable or field '" + compound->name + "' is not declared or accessible");
            }
            target = field->type;
        }
        if (!isNumeric(target) || !isNumeric(exprType(compound->amount.get()))) {
            error("compound arithmetic requires numeric operands");
        }
        return;
    }
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        const ValueType conditionType = exprType(ifStmt->condition.get());
        if (conditionType != ValueType::Bool && conditionType != ValueType::Any) error("if condition must be boolean");
        for (auto& child : ifStmt->thenBlock->statements) statement(child.get());
        if (ifStmt->elseBlock) for (auto& child : ifStmt->elseBlock->statements) statement(child.get());
        return;
    }
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        const ValueType conditionType = exprType(whileStmt->condition.get());
        if (conditionType != ValueType::Bool && conditionType != ValueType::Any) error("while condition must be boolean");
        ++loopDepth;
        for (auto& child : whileStmt->body->statements) statement(child.get());
        --loopDepth;
        return;
    }
    if (auto* repeat = dynamic_cast<RepeatStmt*>(stmt)) {
        if (!isNumeric(exprType(repeat->count.get()))) error("repeat count must be numeric");
        ++loopDepth;
        for (auto& child : repeat->body->statements) statement(child.get());
        --loopDepth;
        return;
    }
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        statement(forStmt->init.get());
        const ValueType conditionType = exprType(forStmt->condition.get());
        if (conditionType != ValueType::Bool && conditionType != ValueType::Any) error("for-loop condition must be boolean");
        statement(forStmt->increment.get());
        ++loopDepth;
        for (auto& child : forStmt->body->statements) statement(child.get());
        --loopDepth;
        return;
    }
    if (auto* each = dynamic_cast<ForEachStmt*>(stmt)) {
        if (exprType(each->collection.get()) != ValueType::List) error("each-in loop requires a list");
        if (declared.count(each->name)) error("iteration variable '" + each->name + "' is already declared");
        declared[each->name] = ValueType::Any;
        ++loopDepth;
        for (auto& child : each->body->statements) statement(child.get());
        --loopDepth;
        declared.erase(each->name);
        return;
    }
    if (auto* mutation = dynamic_cast<ListMutationStmt*>(stmt)) {
        const auto list = declared.find(mutation->name);
        if (list == declared.end() || list->second != ValueType::List) error("'" + mutation->name + "' is not a declared list");
        if (fixedVariables.count(mutation->name)) error("cannot modify fixed list '" + mutation->name + "'");
        if (mutation->value) exprType(mutation->value.get());
        return;
    }
    if (auto* check = dynamic_cast<CheckStmt*>(stmt)) {
        const ValueType checkedType = exprType(check->value.get());
        for (auto& item : check->cases) {
            const ValueType caseType = exprType(item.value.get());
            if (!(assignable(checkedType, caseType) && assignable(caseType, checkedType))) {
                error("check value and when case have incompatible types");
            }
            for (auto& child : item.body->statements) statement(child.get());
        }
        if (check->otherwise) {
            for (auto& child : check->otherwise->statements) statement(child.get());
        }
        return;
    }
    if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
        for (auto& child : block->statements) statement(child.get());
        return;
    }
    error("unknown statement");
}

void SemanticAnalyzer::analyze(const Program& program) {
    declared.clear();
    declaredClasses.clear();
    declaredEnums.clear();
    classes.clear();
    structs.clear();
    enums.clear();
    functions.clear();
    fixedVariables.clear();
    currentClass.clear();
    insideConstructor = false;
    superCallAllowed = false;
    for (const auto& stmt : program) {
        if (auto* klass = dynamic_cast<const ClassStmt*>(stmt.get())) {
            if (classes.count(klass->name) || structs.count(klass->name) || enums.count(klass->name)) {
                error("class '" + klass->name + "' is declared more than once");
            }
            classes.emplace(klass->name, klass);
        } else if (auto* structure = dynamic_cast<const StructStmt*>(stmt.get())) {
            if (classes.count(structure->name) || structs.count(structure->name) ||
                enums.count(structure->name)) {
                error("type '" + structure->name + "' is declared more than once");
            }
            structs.emplace(structure->name, structure);
        } else if (auto* enumeration = dynamic_cast<const EnumStmt*>(stmt.get())) {
            if (classes.count(enumeration->name) || structs.count(enumeration->name) ||
                enums.count(enumeration->name)) {
                error("type '" + enumeration->name + "' is declared more than once");
            }
            enums.emplace(enumeration->name, enumeration);
        } else if (auto* function = dynamic_cast<const FunctionStmt*>(stmt.get())) {
            if (functions.count(function->name)) {
                error("function '" + function->name + "' is declared more than once");
            }
            functions.emplace(function->name, function);
        }
    }
    for (const auto& stmt : program) {
        if (auto* function = dynamic_cast<const FunctionStmt*>(stmt.get())) {
            const auto savedDeclared = declared;
            const auto savedClasses = declaredClasses;
            const auto savedEnums = declaredEnums;
            const auto savedFixed = fixedVariables;
            const auto savedGenericTypes = declaredGenericTypes;
            const ValueType savedReturnType = currentReturnType;
            const std::string savedGenericReturnType = currentGenericReturnType;
            const bool savedInsideFunction = insideFunction;
            declared.clear();
            declaredClasses.clear();
            declaredEnums.clear();
            fixedVariables.clear();
            declaredGenericTypes.clear();
            for (const auto& global : program) {
                if (auto* declaration = dynamic_cast<const DeclarationStmt*>(global.get())) {
                    declared[declaration->name] = declaration->type;
                } else if (auto* initialize = dynamic_cast<const InitializeStmt*>(global.get())) {
                    declared[initialize->name] = initialize->type;
                } else if (auto* initialize = dynamic_cast<const NamedInitializeStmt*>(global.get())) {
                    if (classes.count(initialize->typeName) || structs.count(initialize->typeName)) {
                        declared[initialize->name] = ValueType::Object;
                        declaredClasses[initialize->name] = initialize->typeName;
                    } else if (enums.count(initialize->typeName)) {
                        declared[initialize->name] = ValueType::Enum;
                        declaredEnums[initialize->name] = initialize->typeName;
                    }
                } else if (auto* instance = dynamic_cast<const InstantiateStmt*>(global.get())) {
                    declared[instance->name] = ValueType::Object;
                    declaredClasses[instance->name] = instance->className;
                } else if (auto* fix = dynamic_cast<const FixStmt*>(global.get())) {
                    fixedVariables.insert(fix->name);
                }
            }
            currentReturnType = function->returnType;
            currentGenericReturnType = function->genericReturnType;
            insideFunction = true;
            std::unordered_set<std::string> genericNames;
            for (const auto& genericType : function->genericTypeParameters) {
                if (!genericNames.insert(genericType).second) {
                    error("generic type parameter '" + genericType + "' is repeated");
                }
                const bool usedByParameter = std::any_of(
                    function->parameters.begin(), function->parameters.end(),
                    [&](const Parameter& parameter) {
                        return parameter.genericType == genericType;
                    });
                if (!usedByParameter) {
                    error("generic type '" + genericType +
                          "' must appear in at least one function parameter");
                }
            }
            if (!function->genericReturnType.empty() &&
                !genericNames.count(function->genericReturnType)) {
                error("generic return type '" + function->genericReturnType +
                      "' is not declared by function '" + function->name + "'");
            }
            std::unordered_set<std::string> parameterNames;
            for (const auto& parameter : function->parameters) {
                if (!parameterNames.insert(parameter.name).second) {
                    error("parameter '" + parameter.name + "' is declared more than once");
                }
                declared[parameter.name] = parameter.type;
                if (!parameter.genericType.empty()) {
                    declaredGenericTypes[parameter.name] = parameter.genericType;
                }
            }
            for (auto& child : function->body->statements) statement(child.get());
            declared = savedDeclared;
            declaredClasses = savedClasses;
            declaredEnums = savedEnums;
            fixedVariables = savedFixed;
            declaredGenericTypes = savedGenericTypes;
            currentReturnType = savedReturnType;
            currentGenericReturnType = savedGenericReturnType;
            insideFunction = savedInsideFunction;
        }
    }
    std::unordered_map<std::string, std::string> enumMemberOwners;
    for (const auto& [enumName, definition] : enums) {
        std::unordered_map<std::string, bool> memberNames;
        for (const auto& member : definition->members) {
            if (!memberNames.emplace(member, true).second) {
                error("enum member '" + member + "' is repeated in '" + enumName + "'");
            }
            if (!enumMemberOwners.emplace(member, enumName).second) {
                error("enum member '" + member + "' is ambiguous across enum definitions");
            }
        }
    }
    for (const auto& stmt : program) {
        if (auto* structure = dynamic_cast<const StructStmt*>(stmt.get())) {
            std::unordered_map<std::string, bool> fieldNames;
            for (const auto& field : structure->fields) {
                if (!fieldNames.emplace(field.name, true).second) {
                    error("field '" + field.name + "' is repeated in struct '" + structure->name + "'");
                }
            }
        }
    }
    for (const auto& stmt : program) {
        if (auto* klass = dynamic_cast<const ClassStmt*>(stmt.get())) validateClass(*klass);
    }
    for (auto& stmt : program) statement(stmt.get());
}
