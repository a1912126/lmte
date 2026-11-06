#include "interpreter.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
struct ReturnSignal {
    Value value;
};
struct BreakSignal {};
struct ContinueSignal {};

constexpr std::size_t maxLoopIterations = 10000000;
// Nested function/method/constructor calls allowed before reporting likely infinite recursion.
#ifndef PLAINSPEAK_MAX_CALL_DEPTH
#define PLAINSPEAK_MAX_CALL_DEPTH 10000
#endif
constexpr int maxCallDepth = PLAINSPEAK_MAX_CALL_DEPTH;

template <typename F>
struct ScopeExit {
    F action;
    ~ScopeExit() { action(); }
};
template <typename F>
ScopeExit<F> onExit(F action) {
    return ScopeExit<F>{std::move(action)};
}

bool sameValue(const Value& left, const Value& right) {
    if (left.type != right.type) {
        if ((left.type == ValueType::Int || left.type == ValueType::Float) &&
            (right.type == ValueType::Int || right.type == ValueType::Float)) {
            const double a = left.type == ValueType::Int ? static_cast<double>(left.i) : left.f;
            const double b = right.type == ValueType::Int ? static_cast<double>(right.i) : right.f;
            return std::fabs(a - b) < 1e-12;
        }
        return false;
    }
    switch (left.type) {
        case ValueType::Int: return left.i == right.i;
        case ValueType::Float: return std::fabs(left.f - right.f) < 1e-12;
        case ValueType::String: return left.s == right.s;
        case ValueType::Bool: return left.b == right.b;
        case ValueType::Enum:
            return left.enumType == right.enumType && left.enumIndex == right.enumIndex;
        // Lists and objects are compared by reference: a value is always equal to itself.
        case ValueType::List: return left.list == right.list;
        case ValueType::Object: return left.object == right.object;
        default: return false;
    }
}

// Integer arithmetic that reports overflow instead of invoking undefined behaviour.
long long integerArithmetic(const std::string& op, long long a, long long b) {
    long long result = 0;
    if (op == "+") {
        if (__builtin_add_overflow(a, b, &result)) throw std::runtime_error("integer overflow");
        return result;
    }
    if (op == "-") {
        if (__builtin_sub_overflow(a, b, &result)) throw std::runtime_error("integer overflow");
        return result;
    }
    if (op == "*") {
        if (__builtin_mul_overflow(a, b, &result)) throw std::runtime_error("integer overflow");
        return result;
    }
    if (op == "/") {
        if (b == 0) throw std::runtime_error("division by zero");
        if (a == LLONG_MIN && b == -1) throw std::runtime_error("integer overflow");
        return a / b;
    }
    if (op == "mod") {
        if (b == 0) throw std::runtime_error("modulo by zero");
        if (b == -1) return 0;
        return a % b;
    }
    throw std::runtime_error("unsupported arithmetic operation '" + op + "'");
}

double floatArithmetic(const std::string& op, double a, double b) {
    if (op == "+") return a + b;
    if (op == "-") return a - b;
    if (op == "*") return a * b;
    if (op == "/") {
        if (b == 0.0) throw std::runtime_error("division by zero");
        return a / b;
    }
    if (op == "mod") {
        if (b == 0.0) throw std::runtime_error("modulo by zero");
        return std::fmod(a, b);
    }
    throw std::runtime_error("unsupported arithmetic operation '" + op + "'");
}

std::string compoundOperator(const std::string& command) {
    if (command == "add") return "+";
    if (command == "subtract") return "-";
    if (command == "multiply") return "*";
    if (command == "divide") return "/";
    return "mod";
}
}

bool Interpreter::numeric(ValueType t) {
    return t == ValueType::Int || t == ValueType::Float;
}

double Interpreter::asDouble(const Value& value) const {
    return value.type == ValueType::Int ? static_cast<double>(value.i) : value.f;
}

Interpreter::Slot Interpreter::resolve(const std::string& name) {
    Slot slot;
    auto local = frame->values.find(name);
    if (local != frame->values.end()) {
        slot.frame = frame;
        slot.value = &local->second;
        return slot;
    }
    if (activeObject) {
        auto field = activeObject->fields.find(name);
        if (field != activeObject->fields.end()) {
            slot.value = &field->second;
            slot.isField = true;
            return slot;
        }
    }
    if (frame != &context.globals) {
        auto global = context.globals.values.find(name);
        if (global != context.globals.values.end()) {
            slot.frame = &context.globals;
            slot.value = &global->second;
        }
    }
    return slot;
}

std::shared_ptr<std::vector<Value>> Interpreter::listNamed(const std::string& name) {
    const Slot slot = resolve(name);
    if (!slot.value || slot.value->type != ValueType::List || !slot.value->list) {
        throw std::runtime_error("'" + name + "' is not a list");
    }
    return slot.value->list;
}

Value Interpreter::eval(const Expr* e) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(e)) return literal->value;

    if (auto* variable = dynamic_cast<const VariableExpr*>(e)) {
        const Slot slot = resolve(variable->name);
        if (slot.value) return *slot.value;
        for (const auto& [enumName, definition] : context.enums) {
            auto member = std::find(definition->members.begin(), definition->members.end(),
                                    variable->name);
            if (member != definition->members.end()) {
                return Value::makeEnum(
                    enumName, variable->name,
                    static_cast<long long>(std::distance(definition->members.begin(), member)));
            }
        }
        throw std::runtime_error("undefined variable or field '" + variable->name + "'");
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(e)) {
        Value object = eval(member->object.get());
        if (object.type != ValueType::Object || !object.object) {
            throw std::runtime_error("field access requires a class instance");
        }
        auto field = object.object->fields.find(member->field);
        if (field == object.object->fields.end()) {
            throw std::runtime_error("undefined field '" + member->field + "'");
        }
        return field->second;
    }

    if (auto* list = dynamic_cast<const ListExpr*>(e)) {
        const auto values = listNamed(list->name);
        if (list->operation == ListExpr::Operation::Length) {
            return Value::makeInt(static_cast<long long>(values->size()));
        }
        if (list->operation == ListExpr::Operation::Top) {
            if (values->empty()) throw std::runtime_error("top of an empty list");
            return values->back();
        }
        const Value indexValue = eval(list->index.get());
        if (indexValue.type != ValueType::Int) throw std::runtime_error("list index must be an integer");
        const long long index = indexValue.i;
        if (index < 0 || static_cast<unsigned long long>(index) >= values->size()) {
            throw std::runtime_error("list index out of range");
        }
        return values->at(static_cast<std::size_t>(index));
    }

    if (auto* call = dynamic_cast<const CallExpr*>(e)) {
        const auto function = context.functions.find(call->name);
        if (function == context.functions.end()) {
            throw std::runtime_error("undefined function '" + call->name + "'");
        }
        std::vector<Value> arguments;
        for (const auto& argument : call->arguments) arguments.push_back(eval(argument.get()));
        return invokeFunction(*function->second, arguments);
    }

    if (auto* unary = dynamic_cast<const UnaryExpr*>(e)) {
        Value value = eval(unary->expr.get());
        if (unary->op == "not") return Value::makeBool(!truth(value));
        if (!numeric(value.type)) {
            throw std::runtime_error("unary '" + unary->op + "' requires a number");
        }
        if (unary->op == "+") return value;
        if (value.type == ValueType::Int) {
            if (value.i == LLONG_MIN) throw std::runtime_error("integer overflow");
            return Value::makeInt(-value.i);
        }
        return Value::makeFloat(-value.f);
    }

    auto* binary = dynamic_cast<const BinaryExpr*>(e);
    if (!binary) throw std::runtime_error("invalid expression node");

    Value left = eval(binary->left.get());
    const std::string& op = binary->op;
    if (op == "and") return Value::makeBool(truth(left) && truth(eval(binary->right.get())));
    if (op == "or") return Value::makeBool(truth(left) || truth(eval(binary->right.get())));
    Value right = eval(binary->right.get());

    if (op == "+" && left.type == ValueType::String && right.type == ValueType::String) {
        return Value::makeString(left.s + right.s);
    }

    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "mod") {
        if (!numeric(left.type) || !numeric(right.type)) {
            throw std::runtime_error(op == "+" ? "operator '+' requires two numbers or two strings"
                                               : "operator '" + op + "' requires numeric operands");
        }
        if (left.type == ValueType::Int && right.type == ValueType::Int) {
            return Value::makeInt(integerArithmetic(op, left.i, right.i));
        }
        return Value::makeFloat(floatArithmetic(op, asDouble(left), asDouble(right)));
    }

    if (op == "==" || op == "!=") {
        const bool equal = sameValue(left, right);
        return Value::makeBool(op == "==" ? equal : !equal);
    }

    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        int order = 0;
        if (left.type == ValueType::Int && right.type == ValueType::Int) {
            // Compare integers exactly: doubles cannot hold every value above 2^53.
            order = left.i < right.i ? -1 : (left.i > right.i ? 1 : 0);
        } else if (numeric(left.type) && numeric(right.type)) {
            const double a = asDouble(left);
            const double b = asDouble(right);
            order = a < b ? -1 : (a > b ? 1 : 0);
        } else if (left.type == ValueType::String && right.type == ValueType::String) {
            order = left.s < right.s ? -1 : (left.s > right.s ? 1 : 0);
        } else {
            throw std::runtime_error("comparison requires two numbers or two strings");
        }
        bool result = false;
        if (op == "<") result = order < 0;
        else if (op == "<=") result = order <= 0;
        else if (op == ">") result = order > 0;
        else result = order >= 0;
        return Value::makeBool(result);
    }

    throw std::runtime_error("unsupported binary operation '" + op + "'");
}

bool Interpreter::truth(const Value& value) const {
    if (value.type == ValueType::Bool) return value.b;
    throw std::runtime_error("condition must be a boolean value, found " + typeName(value.type));
}

Value Interpreter::castTo(ValueType target, const Value& value, const std::string& name) const {
    if (target == ValueType::Any) return value;
    if (target == value.type) return value;
    if (target == ValueType::Float && value.type == ValueType::Int) {
        return Value::makeFloat(static_cast<double>(value.i));
    }
    throw std::runtime_error("cannot assign " + typeName(value.type) + " value to " +
                             typeName(target) + " " + name);
}

bool Interpreter::derivesFrom(const std::string& className, const std::string& baseName) const {
    std::string name = className;
    for (std::size_t depth = 0; !name.empty() && depth <= context.classes.size(); ++depth) {
        if (name == baseName) return true;
        auto it = context.classes.find(name);
        if (it == context.classes.end()) break;
        name = it->second->baseName;
    }
    return false;
}

// Converts a value for storage in an existing variable or field, checking its declared type.
Value Interpreter::assignTo(const Slot& slot, const std::string& name, const Value& value) const {
    if (slot.isField) return castTo(slot.value->type, value, "field '" + name + "'");
    const ValueType type = slot.frame->types.at(name);
    Value converted = castTo(type, value, "variable '" + name + "'");
    auto declaredClass = slot.frame->classNames.find(name);
    if (declaredClass != slot.frame->classNames.end()) {
        if (type == ValueType::Object && converted.object &&
            !derivesFrom(converted.object->className, declaredClass->second)) {
            throw std::runtime_error("cannot assign " + converted.object->className +
                                     " object to " + declaredClass->second + " variable '" +
                                     name + "'");
        }
        if (type == ValueType::Enum && converted.enumType != declaredClass->second) {
            throw std::runtime_error("cannot assign " + converted.enumType + " value to " +
                                     declaredClass->second + " variable '" + name + "'");
        }
    }
    return converted;
}

Value Interpreter::defaultValue(ValueType type) const {
    if (type == ValueType::Int) return Value::makeInt(0);
    if (type == ValueType::Float) return Value::makeFloat(0.0);
    if (type == ValueType::String) return Value::makeString("");
    if (type == ValueType::Bool) return Value::makeBool(false);
    if (type == ValueType::List) return Value::makeList();
    return {};
}

std::shared_ptr<ObjectInstance> Interpreter::newObject(const std::string& className) {
    auto object = std::make_shared<ObjectInstance>();
    object->className = className;
    auto structure = context.structs.find(className);
    if (structure != context.structs.end()) {
        for (const auto& field : structure->second->fields) {
            object->fields[field.name] = defaultValue(field.type);
        }
        return object;
    }
    std::vector<const ClassStmt*> lineage;
    for (std::string name = className; !name.empty();) {
        const ClassStmt* klass = context.classes.at(name);
        lineage.push_back(klass);
        name = klass->baseName;
    }
    for (auto it = lineage.rbegin(); it != lineage.rend(); ++it) {
        for (const auto& field : (*it)->fields) object->fields[field.name] = defaultValue(field.type);
    }
    return object;
}

void Interpreter::runBody(Frame& local, const std::shared_ptr<ObjectInstance>& object,
                          const std::string& className, BlockStmt* body, Value* returned,
                          bool* didReturn) {
    // Stop before deep recursion overflows the C++ stack and crashes the process.
    const char marker = 0;
    const auto here = reinterpret_cast<std::uintptr_t>(&marker);
    if (callDepth >= maxCallDepth || (stackBase > here && stackBase - here > stackBudget)) {
        throw std::runtime_error("too many nested calls (" + std::to_string(callDepth) +
                                 " deep); possible infinite recursion");
    }
    Frame* const savedFrame = frame;
    const auto savedObject = activeObject;
    const std::string savedClass = activeClass;
    ++callDepth;
    auto restore = onExit([&] {
        frame = savedFrame;
        activeObject = savedObject;
        activeClass = savedClass;
        --callDepth;
    });
    frame = &local;
    activeObject = object;
    activeClass = className;
    try {
        execBlock(body);
    } catch (ReturnSignal& signal) {
        if (returned) *returned = std::move(signal.value);
        if (didReturn) *didReturn = true;
    }
}

void Interpreter::invokeMethod(std::shared_ptr<ObjectInstance> object,
                               const std::string& methodName,
                               const std::vector<Value>& arguments) {
    std::string lookupClass = object->className;
    const ClassMethod* method = nullptr;
    std::string declaringClass;
    while (!lookupClass.empty()) {
        auto it = context.classes.find(lookupClass);
        if (it == context.classes.end()) break;
        for (const auto& candidate : it->second->methods) {
            if (candidate.name == methodName) {
                method = &candidate;
                declaringClass = lookupClass;
                break;
            }
        }
        if (method) break;
        lookupClass = it->second->baseName;
    }
    if (!method || method->isAbstract) {
        throw std::runtime_error("method '" + methodName + "' has no concrete implementation");
    }

    Frame local;
    for (std::size_t i = 0; i < method->parameters.size(); ++i) {
        const auto& parameter = method->parameters[i];
        local.types[parameter.name] = parameter.type;
        local.values[parameter.name] =
            castTo(parameter.type, arguments.at(i), "parameter '" + parameter.name + "'");
    }
    runBody(local, object, declaringClass, method->body.get());
}

void Interpreter::invokeConstructor(const std::shared_ptr<ObjectInstance>& object,
                                    const ClassStmt& klass,
                                    const std::vector<Value>& arguments) {
    if (!klass.hasConstructor) {
        // A class without a constructor uses its parent's.
        if (!klass.baseName.empty()) {
            invokeConstructor(object, *context.classes.at(klass.baseName), arguments);
        }
        return;
    }
    const auto& statements = klass.constructorBody->statements;
    const bool explicitSuper = !statements.empty() &&
        dynamic_cast<const SuperConstructorCallStmt*>(statements.front().get()) != nullptr;
    if (!explicitSuper && !klass.baseName.empty()) {
        // No super(...) call: run the inherited no-argument constructor first, as most
        // object-oriented languages do. Semantic analysis rejects this when the inherited
        // constructor needs arguments.
        invokeConstructor(object, *context.classes.at(klass.baseName), {});
    }

    Frame local;
    for (std::size_t i = 0; i < klass.constructorParameters.size(); ++i) {
        const auto& parameter = klass.constructorParameters[i];
        local.types[parameter.name] = parameter.type;
        local.values[parameter.name] =
            castTo(parameter.type, arguments.at(i), "parameter '" + parameter.name + "'");
    }
    runBody(local, object, klass.name, klass.constructorBody.get());
}

Value Interpreter::invokeFunction(const FunctionStmt& function,
                                  const std::vector<Value>& arguments) {
    if (arguments.size() != function.parameters.size()) {
        throw std::runtime_error("wrong number of arguments for function '" + function.name + "'");
    }

    // Each call gets its own frame. Names that are not local resolve to globals,
    // never to the caller's variables or to the fields of a calling method's object.
    Frame local;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& parameter = function.parameters[i];
        local.types[parameter.name] = parameter.type;
        local.values[parameter.name] =
            castTo(parameter.type, arguments[i], "parameter '" + parameter.name + "'");
    }

    Value result;
    bool returned = false;
    runBody(local, nullptr, "", function.body.get(), &result, &returned);
    if (function.returnType == ValueType::Void) return {};
    if (!returned) {
        throw std::runtime_error("function '" + function.name + "' completed without returning a value");
    }
    return castTo(function.returnType, result, "return value of function '" + function.name + "'");
}

void Interpreter::declare(const std::string& name, ValueType type, Value value,
                          const std::string& className) {
    frame->types[name] = type;
    frame->values[name] = std::move(value);
    if (className.empty()) frame->classNames.erase(name);
    else frame->classNames[name] = className;
    frame->constants.erase(name);
}

void Interpreter::loopGuard(std::size_t& iterations) const {
    if (++iterations > maxLoopIterations) throw std::runtime_error("possible infinite loop");
}

void Interpreter::exec(Stmt* stmt) {
    if (dynamic_cast<ClassStmt*>(stmt) || dynamic_cast<StructStmt*>(stmt) ||
        dynamic_cast<EnumStmt*>(stmt) || dynamic_cast<FunctionStmt*>(stmt)) return;

    if (auto* fix = dynamic_cast<FixStmt*>(stmt)) {
        const Slot slot = resolve(fix->name);
        if (!slot.value || slot.isField) {
            throw std::runtime_error("cannot fix undeclared variable '" + fix->name + "'");
        }
        slot.frame->constants.insert(fix->name);
        return;
    }

    if (auto* returned = dynamic_cast<ReturnStmt*>(stmt)) {
        throw ReturnSignal{returned->expression ? eval(returned->expression.get()) : Value{}};
    }

    if (auto* control = dynamic_cast<LoopControlStmt*>(stmt)) {
        if (control->isBreak) throw BreakSignal{};
        throw ContinueSignal{};
    }

    if (auto* declaration = dynamic_cast<DeclarationStmt*>(stmt)) {
        Value value = declaration->initializer
                          ? castTo(declaration->type, eval(declaration->initializer.get()),
                                   "variable '" + declaration->name + "'")
                          : defaultValue(declaration->type);
        declare(declaration->name, declaration->type, std::move(value));
        return;
    }

    if (auto* initialize = dynamic_cast<InitializeStmt*>(stmt)) {
        declare(initialize->name, initialize->type, defaultValue(initialize->type));
        return;
    }

    if (auto* initialize = dynamic_cast<NamedInitializeStmt*>(stmt)) {
        auto enumeration = context.enums.find(initialize->typeName);
        if (enumeration != context.enums.end()) {
            declare(initialize->name, ValueType::Enum,
                    Value::makeEnum(initialize->typeName, enumeration->second->members.front(), 0),
                    initialize->typeName);
            return;
        }
        auto object = newObject(initialize->typeName);
        auto klass = context.classes.find(initialize->typeName);
        if (klass != context.classes.end()) invokeConstructor(object, *klass->second, {});
        declare(initialize->name, ValueType::Object, Value::makeObject(std::move(object)),
                initialize->typeName);
        return;
    }

    if (auto* instance = dynamic_cast<InstantiateStmt*>(stmt)) {
        std::vector<Value> arguments;
        arguments.reserve(instance->arguments.size());
        for (const auto& argument : instance->arguments) arguments.push_back(eval(argument.get()));
        auto object = newObject(instance->className);
        invokeConstructor(object, *context.classes.at(instance->className), arguments);
        declare(instance->name, ValueType::Object, Value::makeObject(std::move(object)),
                instance->className);
        return;
    }

    if (auto* assignment = dynamic_cast<AssignmentStmt*>(stmt)) {
        const Value value = eval(assignment->expr.get());
        const Slot slot = resolve(assignment->name);
        if (!slot.value) {
            throw std::runtime_error("variable or field '" + assignment->name + "' has not been initialized");
        }
        if (!slot.isField && slot.frame->constants.count(assignment->name)) {
            throw std::runtime_error("cannot assign to constant '" + assignment->name + "'");
        }
        *slot.value = assignTo(slot, assignment->name, value);
        return;
    }

    if (auto* assignment = dynamic_cast<MemberAssignmentStmt*>(stmt)) {
        const Value value = eval(assignment->expr.get());
        const Slot slot = resolve(assignment->objectName);
        if (!slot.value || slot.value->type != ValueType::Object || !slot.value->object) {
            throw std::runtime_error("'" + assignment->objectName + "' is not a class instance");
        }
        auto& fields = slot.value->object->fields;
        auto field = fields.find(assignment->field);
        if (field == fields.end()) {
            throw std::runtime_error("undefined field '" + assignment->field + "'");
        }
        field->second = castTo(field->second.type, value, "field '" + assignment->field + "'");
        return;
    }

    if (auto* mutation = dynamic_cast<ListMutationStmt*>(stmt)) {
        const Value appended = mutation->value ? eval(mutation->value.get()) : Value{};
        const Slot slot = resolve(mutation->name);
        if (slot.value && !slot.isField && slot.frame->constants.count(mutation->name)) {
            throw std::runtime_error("cannot modify list constant '" + mutation->name + "'");
        }
        const auto list = listNamed(mutation->name);
        if (mutation->operation == ListMutationStmt::Operation::Append) {
            list->push_back(appended);
        } else {
            if (list->empty()) throw std::runtime_error("cannot remove item from an empty list");
            list->pop_back();
        }
        return;
    }

    if (auto* check = dynamic_cast<CheckStmt*>(stmt)) {
        const Value checked = eval(check->value.get());
        for (const auto& item : check->cases) {
            if (sameValue(checked, eval(item.value.get()))) {
                execBlock(item.body.get());
                return;
            }
        }
        if (check->otherwise) execBlock(check->otherwise.get());
        return;
    }

    if (auto* call = dynamic_cast<MethodCallStmt*>(stmt)) {
        std::vector<Value> arguments;
        arguments.reserve(call->arguments.size());
        for (const auto& argument : call->arguments) arguments.push_back(eval(argument.get()));
        const Slot slot = resolve(call->objectName);
        if (!slot.value || slot.value->type != ValueType::Object || !slot.value->object) {
            throw std::runtime_error("'" + call->objectName + "' is not a class instance");
        }
        invokeMethod(slot.value->object, call->method, arguments);
        return;
    }

    if (auto* superCall = dynamic_cast<SuperConstructorCallStmt*>(stmt)) {
        if (!activeObject) {
            throw std::runtime_error("'super()' can only be used inside a constructor");
        }
        const auto current = context.classes.find(activeClass);
        if (current == context.classes.end() || current->second->baseName.empty()) {
            throw std::runtime_error("'super()' requires a base class");
        }
        std::vector<Value> arguments;
        arguments.reserve(superCall->arguments.size());
        for (const auto& argument : superCall->arguments) {
            arguments.push_back(eval(argument.get()));
        }
        const auto baseClass = context.classes.find(current->second->baseName);
        if (baseClass == context.classes.end()) {
            throw std::runtime_error("undefined base class '" + current->second->baseName + "'");
        }
        invokeConstructor(activeObject, *baseClass->second, arguments);
        return;
    }

    if (auto* output = dynamic_cast<OutputStmt*>(stmt)) {
        std::cout << eval(output->expr.get()).toString() << '\n';
        return;
    }

    if (auto* expression = dynamic_cast<ExpressionStmt*>(stmt)) {
        eval(expression->expression.get());
        return;
    }

    if (auto* input = dynamic_cast<InputStmt*>(stmt)) {
        const Value prompt = eval(input->prompt.get());
        std::cout << prompt.toString() << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("failed to read input for '" + input->name + "'");
        }

        const Slot slot = resolve(input->name);
        if (!slot.value) throw std::runtime_error("ask target '" + input->name + "' is not declared");
        if (!slot.isField && slot.frame->constants.count(input->name)) {
            throw std::runtime_error("cannot assign input to constant '" + input->name + "'");
        }
        const ValueType targetType =
            slot.isField ? slot.value->type : slot.frame->types.at(input->name);

        Value converted;
        if (targetType == ValueType::String) {
            converted = Value::makeString(line);
        } else if (targetType == ValueType::Bool) {
            if (line == "true") converted = Value::makeBool(true);
            else if (line == "false") converted = Value::makeBool(false);
            else throw std::runtime_error(
                "invalid bool input for '" + input->name + "': expected 'true' or 'false'");
        } else {
            std::istringstream inputStream(line);
            if (targetType == ValueType::Int) {
                long long value = 0;
                if (!(inputStream >> value)) {
                    throw std::runtime_error("invalid integer input for '" + input->name + "'");
                }
                inputStream >> std::ws;
                if (!inputStream.eof()) {
                    throw std::runtime_error("invalid integer input for '" + input->name + "'");
                }
                converted = Value::makeInt(value);
            } else if (targetType == ValueType::Float) {
                double value = 0.0;
                if (!(inputStream >> value)) {
                    throw std::runtime_error("invalid float input for '" + input->name + "'");
                }
                inputStream >> std::ws;
                if (!inputStream.eof() || !std::isfinite(value)) {
                    throw std::runtime_error("invalid float input for '" + input->name + "'");
                }
                converted = Value::makeFloat(value);
            } else {
                throw std::runtime_error("ask target '" + input->name + "' has an unsupported type");
            }
        }
        *slot.value = std::move(converted);
        return;
    }

    if (auto* compound = dynamic_cast<AddToStmt*>(stmt)) {
        const Value amount = eval(compound->amount.get());
        const Slot slot = resolve(compound->name);
        if (!slot.value) {
            throw std::runtime_error("variable or field '" + compound->name + "' has not been initialized");
        }
        if (!slot.isField && slot.frame->constants.count(compound->name)) {
            throw std::runtime_error("cannot modify constant '" + compound->name + "'");
        }
        const Value& current = *slot.value;
        if (!numeric(current.type) || !numeric(amount.type)) {
            throw std::runtime_error("'" + compound->op + "' requires numeric operands");
        }
        const std::string op = compoundOperator(compound->op);
        Value updated;
        if (current.type == ValueType::Int) {
            // Integer variables use exact integer arithmetic (with overflow checks).
            if (amount.type != ValueType::Int) {
                throw std::runtime_error("'" + compound->op + "' with a float amount cannot update int variable '" +
                                         compound->name + "'");
            }
            updated = Value::makeInt(integerArithmetic(op, current.i, amount.i));
        } else {
            updated = Value::makeFloat(floatArithmetic(op, current.f, asDouble(amount)));
        }
        *slot.value = std::move(updated);
        return;
    }

    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (truth(eval(ifStmt->condition.get()))) execBlock(ifStmt->thenBlock.get());
        else if (ifStmt->elseBlock) execBlock(ifStmt->elseBlock.get());
        return;
    }

    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        std::size_t iterations = 0;
        while (truth(eval(whileStmt->condition.get()))) {
            try {
                execBlock(whileStmt->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            loopGuard(iterations);
        }
        return;
    }

    if (auto* repeat = dynamic_cast<RepeatStmt*>(stmt)) {
        const Value countValue = eval(repeat->count.get());
        long long count = 0;
        if (countValue.type == ValueType::Int) {
            count = countValue.i;
        } else if (countValue.type == ValueType::Float) {
            if (!std::isfinite(countValue.f)) throw std::runtime_error("repeat count must be a finite number");
            const double whole = std::trunc(countValue.f);
            // Converting an out-of-range double to an integer is undefined behaviour.
            count = whole >= 9.0e18 ? LLONG_MAX : (whole <= -9.0e18 ? LLONG_MIN : static_cast<long long>(whole));
        } else {
            throw std::runtime_error("repeat count must be a number");
        }
        if (count < 0) throw std::runtime_error("repeat count cannot be negative");
        std::size_t iterations = 0;
        for (long long k = 0; k < count; ++k) {
            loopGuard(iterations);
            try {
                execBlock(repeat->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
        }
        return;
    }

    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        exec(forStmt->init.get());
        std::size_t iterations = 0;
        while (truth(eval(forStmt->condition.get()))) {
            try {
                execBlock(forStmt->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            exec(forStmt->increment.get());
            loopGuard(iterations);
        }
        return;
    }

    if (auto* each = dynamic_cast<ForEachStmt*>(stmt)) {
        const Value collection = eval(each->collection.get());
        if (collection.type != ValueType::List || !collection.list) {
            throw std::runtime_error("each-in loop requires a list");
        }
        Frame& scope = *frame;
        const bool hadType = scope.types.count(each->name) != 0;
        const bool hadValue = scope.values.count(each->name) != 0;
        const ValueType previousType = hadType ? scope.types.at(each->name) : ValueType::Any;
        const Value previousValue = hadValue ? scope.values.at(each->name) : Value{};
        // The loop variable exists only inside the loop.
        auto restore = onExit([&] {
            if (hadType) scope.types[each->name] = previousType;
            else scope.types.erase(each->name);
            if (hadValue) scope.values[each->name] = previousValue;
            else scope.values.erase(each->name);
        });
        scope.types[each->name] = ValueType::Any;
        std::size_t iterations = 0;
        const std::vector<Value> items = *collection.list;
        for (const Value& item : items) {
            scope.values[each->name] = item;
            try {
                execBlock(each->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            loopGuard(iterations);
        }
        return;
    }

    if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
        execBlock(block);
        return;
    }

    throw std::runtime_error("unknown statement");
}

void Interpreter::execBlock(BlockStmt* block) {
    for (auto& stmt : block->statements) exec(stmt.get());
}

void Interpreter::run(const Program& program) {
    const char marker = 0;
    stackBase = reinterpret_cast<std::uintptr_t>(&marker);
    context = {};
    frame = &context.globals;
    activeObject.reset();
    activeClass.clear();
    callDepth = 0;
    for (const auto& stmt : program) {
        if (auto* klass = dynamic_cast<ClassStmt*>(stmt.get())) context.classes[klass->name] = klass;
        else if (auto* structure = dynamic_cast<StructStmt*>(stmt.get())) {
            context.structs[structure->name] = structure;
        } else if (auto* enumeration = dynamic_cast<EnumStmt*>(stmt.get())) {
            context.enums[enumeration->name] = enumeration;
        } else if (auto* function = dynamic_cast<FunctionStmt*>(stmt.get())) {
            context.functions[function->name] = function;
        }
    }
    for (auto& stmt : program) exec(stmt.get());
}
