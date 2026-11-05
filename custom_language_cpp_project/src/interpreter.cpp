#include "interpreter.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
struct ReturnSignal {
    Value value;
};
struct BreakSignal {};
struct ContinueSignal {};

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
        default: return false;
    }
}

void collectLocals(const Stmt* statement, std::set<std::string>& names) {
    if (auto* declaration = dynamic_cast<const DeclarationStmt*>(statement)) names.insert(declaration->name);
    else if (auto* initialize = dynamic_cast<const InitializeStmt*>(statement)) names.insert(initialize->name);
    else if (auto* initialize = dynamic_cast<const NamedInitializeStmt*>(statement)) names.insert(initialize->name);
    else if (auto* instance = dynamic_cast<const InstantiateStmt*>(statement)) names.insert(instance->name);
    else if (auto* loop = dynamic_cast<const ForEachStmt*>(statement)) {
        names.insert(loop->name);
        collectLocals(loop->body.get(), names);
    } else if (auto* loop = dynamic_cast<const ForStmt*>(statement)) {
        collectLocals(loop->init.get(), names);
        collectLocals(loop->body.get(), names);
    } else if (auto* loop = dynamic_cast<const WhileStmt*>(statement)) {
        collectLocals(loop->body.get(), names);
    } else if (auto* loop = dynamic_cast<const RepeatStmt*>(statement)) {
        collectLocals(loop->body.get(), names);
    } else if (auto* branch = dynamic_cast<const IfStmt*>(statement)) {
        collectLocals(branch->thenBlock.get(), names);
        if (branch->elseBlock) collectLocals(branch->elseBlock.get(), names);
    } else if (auto* check = dynamic_cast<const CheckStmt*>(statement)) {
        for (const auto& item : check->cases) collectLocals(item.body.get(), names);
        if (check->otherwise) collectLocals(check->otherwise.get(), names);
    } else if (auto* block = dynamic_cast<const BlockStmt*>(statement)) {
        for (const auto& child : block->statements) collectLocals(child.get(), names);
    }
}
}

bool Interpreter::numeric(ValueType t) {
    return t == ValueType::Int || t == ValueType::Float;
}

double Interpreter::asDouble(const Value& value) const {
    return value.type == ValueType::Int ? static_cast<double>(value.i) : value.f;
}

Value Interpreter::eval(const Expr* e) {
    if (auto* literal = dynamic_cast<const LiteralExpr*>(e)) return literal->value;

    if (auto* variable = dynamic_cast<const VariableExpr*>(e)) {
        auto it = context.values.find(variable->name);
        if (it == context.values.end()) {
            if (activeObject) {
                auto field = activeObject->fields.find(variable->name);
                if (field != activeObject->fields.end()) return field->second;
            }
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
        return it->second;
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
        const auto found = context.values.find(list->name);
        if (found == context.values.end() || found->second.type != ValueType::List ||
            !found->second.list) {
            throw std::runtime_error("'" + list->name + "' is not a list");
        }
        const auto values = found->second.list;
        if (list->operation == ListExpr::Operation::Length) {
            return Value::makeInt(static_cast<long long>(values->size()));
        }
        if (list->operation == ListExpr::Operation::Top) {
            if (values->empty()) throw std::runtime_error("top of an empty list");
            return values->back();
        }
        const Value indexValue = eval(list->index.get());
        const long long index = indexValue.type == ValueType::Int ? indexValue.i : -1;
        if (index < 0 || static_cast<std::size_t>(index) >= values->size()) {
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
        if (unary->op == "+") return value;
        if (unary->op == "not") return Value::makeBool(!truth(value));
        if (value.type == ValueType::Int) return Value::makeInt(-value.i);
        if (value.type == ValueType::Float) return Value::makeFloat(-value.f);
        throw std::runtime_error("invalid unary operation");
    }

    auto* binary = dynamic_cast<const BinaryExpr*>(e);
    if (!binary) throw std::runtime_error("invalid expression node");

    Value left = eval(binary->left.get());
    const std::string& op = binary->op;
    if (op == "and") return Value::makeBool(truth(left) && truth(eval(binary->right.get())));
    if (op == "or") return Value::makeBool(truth(left) || truth(eval(binary->right.get())));
    Value right = eval(binary->right.get());

    if (op == "+") {
        if (left.type == ValueType::String && right.type == ValueType::String) {
            return Value::makeString(left.s + right.s);
        }
        if (numeric(left.type) && numeric(right.type)) {
            if (left.type == ValueType::Int && right.type == ValueType::Int) {
                return Value::makeInt(left.i + right.i);
            }
            return Value::makeFloat(asDouble(left) + asDouble(right));
        }
    }

    if (op == "-" || op == "*" || op == "/" || op == "mod") {
        if (!numeric(left.type) || !numeric(right.type)) {
            throw std::runtime_error("arithmetic requires numeric operands");
        }

        const double divisor = asDouble(right);
        if ((op == "/" && std::fabs(divisor) < 1e-15) ||
            (op == "mod" && std::fabs(divisor) < 1e-15)) {
            throw std::runtime_error(op == "/" ? "division by zero" : "modulo by zero");
        }

        if (left.type == ValueType::Int && right.type == ValueType::Int) {
            if (op == "-") return Value::makeInt(left.i - right.i);
            if (op == "*") return Value::makeInt(left.i * right.i);
            if (op == "/") return Value::makeInt(left.i / right.i);
            return Value::makeInt(left.i % right.i);
        }

        const double a = asDouble(left);
        const double b = asDouble(right);
        if (op == "-") return Value::makeFloat(a - b);
        if (op == "*") return Value::makeFloat(a * b);
        if (op == "mod") return Value::makeFloat(std::fmod(a, b));
        return Value::makeFloat(a / b);
    }

    if (op == "==" || op == "!=") {
        const bool equal = sameValue(left, right);
        return Value::makeBool(op == "==" ? equal : !equal);
    }

    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        bool result = false;
        if (numeric(left.type) && numeric(right.type)) {
            const double a = asDouble(left);
            const double b = asDouble(right);
            if (op == "<") result = a < b;
            else if (op == "<=") result = a <= b;
            else if (op == ">") result = a > b;
            else result = a >= b;
        } else if (left.type == ValueType::String && right.type == ValueType::String) {
            if (op == "<") result = left.s < right.s;
            else if (op == "<=") result = left.s <= right.s;
            else if (op == ">") result = left.s > right.s;
            else result = left.s >= right.s;
        } else {
            throw std::runtime_error("invalid comparison");
        }
        return Value::makeBool(result);
    }

    throw std::runtime_error("unsupported binary operation '" + op + "'");
}

bool Interpreter::truth(const Value& value) const {
    if (value.type == ValueType::Bool) return value.b;
    if (value.type == ValueType::Int) return value.i != 0;
    if (value.type == ValueType::Float) return std::fabs(value.f) > 1e-15;
    if (value.type == ValueType::String) return !value.s.empty();
    return false;
}

Value Interpreter::castTo(ValueType target, const Value& value) const {
    if (target == ValueType::Any) return value;
    if (target == value.type) return value;
    if (target == ValueType::Float && value.type == ValueType::Int) {
        return Value::makeFloat(static_cast<double>(value.i));
    }
    throw std::runtime_error("type conversion failed");
}

const ClassField* Interpreter::findField(const std::string& className, const std::string& field) const {
    auto it = context.classes.find(className);
    if (it == context.classes.end()) return nullptr;
    for (const auto& candidate : it->second->fields) {
        if (candidate.name == field) return &candidate;
    }
    return it->second->baseName.empty() ? nullptr : findField(it->second->baseName, field);
}

const ClassMethod* Interpreter::findMethod(const std::string& className, const std::string& method) const {
    auto it = context.classes.find(className);
    if (it == context.classes.end()) return nullptr;
    for (const auto& candidate : it->second->methods) {
        if (candidate.name == method) return &candidate;
    }
    return it->second->baseName.empty() ? nullptr : findMethod(it->second->baseName, method);
}

Value Interpreter::defaultValue(ValueType type) const {
    if (type == ValueType::Int) return Value::makeInt(0);
    if (type == ValueType::Float) return Value::makeFloat(0.0);
    if (type == ValueType::String) return Value::makeString("");
    if (type == ValueType::Bool) return Value::makeBool(false);
    if (type == ValueType::List) return Value::makeList();
    if (type == ValueType::Enum) return {};
    return {};
}

void Interpreter::invokeMethod(const std::shared_ptr<ObjectInstance>& object,
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

    auto savedTypes = context.types;
    auto savedValues = context.values;
    auto savedConstants = context.constants;
    const auto savedObject = activeObject;
    const std::string savedClass = activeClass;
    context.types.clear();
    context.values.clear();
    context.constants.clear();
    for (std::size_t i = 0; i < method->parameters.size(); ++i) {
        const auto& parameter = method->parameters[i];
        context.types[parameter.name] = parameter.type;
        context.values[parameter.name] = castTo(parameter.type, arguments.at(i));
    }
    activeObject = object;
    activeClass = declaringClass;
    try {
        execBlock(method->body.get());
    } catch (const ReturnSignal&) {
        context.types = std::move(savedTypes);
        context.values = std::move(savedValues);
        context.constants = std::move(savedConstants);
        activeObject = savedObject;
        activeClass = savedClass;
    } catch (...) {
        context.types = std::move(savedTypes);
        context.values = std::move(savedValues);
        context.constants = std::move(savedConstants);
        activeObject = savedObject;
        activeClass = savedClass;
        throw;
    }
    context.types = std::move(savedTypes);
    context.values = std::move(savedValues);
    context.constants = std::move(savedConstants);
    activeObject = savedObject;
    activeClass = savedClass;
}

void Interpreter::invokeConstructor(const std::shared_ptr<ObjectInstance>& object,
                                    const ClassStmt& klass,
                                    const std::vector<Value>& arguments) {
    if (!klass.hasConstructor) {
        if (!klass.baseName.empty()) {
            invokeConstructor(object, *context.classes.at(klass.baseName), arguments);
        }
        return;
    }
    auto savedTypes = context.types;
    auto savedValues = context.values;
    auto savedConstants = context.constants;
    const auto savedObject = activeObject;
    const std::string savedClass = activeClass;
    context.types.clear();
    context.values.clear();
    context.constants.clear();
    for (std::size_t i = 0; i < klass.constructorParameters.size(); ++i) {
        const auto& parameter = klass.constructorParameters[i];
        context.types[parameter.name] = parameter.type;
        context.values[parameter.name] = castTo(parameter.type, arguments.at(i));
    }
    activeObject = object;
    activeClass = klass.name;
    try {
        execBlock(klass.constructorBody.get());
    } catch (const ReturnSignal&) {
        context.types = std::move(savedTypes);
        context.values = std::move(savedValues);
        context.constants = std::move(savedConstants);
        activeObject = savedObject;
        activeClass = savedClass;
    } catch (...) {
        context.types = std::move(savedTypes);
        context.values = std::move(savedValues);
        context.constants = std::move(savedConstants);
        activeObject = savedObject;
        activeClass = savedClass;
        throw;
    }
    context.types = std::move(savedTypes);
    context.values = std::move(savedValues);
    context.constants = std::move(savedConstants);
    activeObject = savedObject;
    activeClass = savedClass;
}

Value Interpreter::invokeFunction(const FunctionStmt& function,
                                  const std::vector<Value>& arguments) {
    if (arguments.size() != function.parameters.size()) {
        throw std::runtime_error("wrong number of arguments for function '" + function.name + "'");
    }

    std::set<std::string> localNames;
    for (const auto& parameter : function.parameters) localNames.insert(parameter.name);
    collectLocals(function.body.get(), localNames);

    auto savedTypes = context.types;
    auto savedValues = context.values;
    auto savedClasses = context.classNames;
    auto savedConstants = context.constants;
    auto restoreLocals = [&]() {
        for (const auto& name : localNames) {
            context.types.erase(name);
            context.values.erase(name);
            context.classNames.erase(name);
            context.constants.erase(name);
            if (savedTypes.count(name)) context.types[name] = savedTypes.at(name);
            if (savedValues.count(name)) context.values[name] = savedValues.at(name);
            if (savedClasses.count(name)) context.classNames[name] = savedClasses.at(name);
            if (savedConstants.count(name)) context.constants[name] = savedConstants.at(name);
        }
    };

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& parameter = function.parameters[i];
        context.types[parameter.name] = parameter.type;
        context.values[parameter.name] = castTo(parameter.type, arguments[i]);
    }

    try {
        execBlock(function.body.get());
    } catch (const ReturnSignal& returned) {
        restoreLocals();
        return function.returnType == ValueType::Void
                   ? Value{}
                   : castTo(function.returnType, returned.value);
    } catch (...) {
        restoreLocals();
        throw;
    }
    restoreLocals();
    if (function.returnType != ValueType::Void) {
        throw std::runtime_error("function '" + function.name + "' completed without returning a value");
    }
    return {};
}

void Interpreter::exec(Stmt* stmt) {
    if (dynamic_cast<ClassStmt*>(stmt) || dynamic_cast<StructStmt*>(stmt) ||
        dynamic_cast<EnumStmt*>(stmt) || dynamic_cast<FunctionStmt*>(stmt)) return;

    if (auto* fix = dynamic_cast<FixStmt*>(stmt)) {
        if (!context.values.count(fix->name)) {
            throw std::runtime_error("cannot fix undeclared variable '" + fix->name + "'");
        }
        context.constants[fix->name] = true;
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
        context.types[declaration->name] = declaration->type;
        if (declaration->initializer) {
            context.values[declaration->name] = castTo(
                declaration->type, eval(declaration->initializer.get()));
        } else {
            context.values[declaration->name] = defaultValue(declaration->type);
        }
        return;
    }

    if (auto* initialize = dynamic_cast<InitializeStmt*>(stmt)) {
        context.types[initialize->name] = initialize->type;
        context.values[initialize->name] = initialize->type == ValueType::List
                                               ? Value::makeList()
                                               : defaultValue(initialize->type);
        return;
    }

    if (auto* initialize = dynamic_cast<NamedInitializeStmt*>(stmt)) {
        if (context.enums.count(initialize->typeName)) {
            const EnumStmt& enumeration = *context.enums.at(initialize->typeName);
            context.types[initialize->name] = ValueType::Enum;
            context.classNames[initialize->name] = initialize->typeName;
            context.values[initialize->name] = Value::makeEnum(
                initialize->typeName, enumeration.members.front(), 0);
            return;
        }

        auto object = std::make_shared<ObjectInstance>();
        object->className = initialize->typeName;
        auto structure = context.structs.find(initialize->typeName);
        if (structure != context.structs.end()) {
            for (const auto& field : structure->second->fields) {
                object->fields[field.name] = defaultValue(field.type);
            }
        } else {
            std::vector<std::string> lineage;
            std::string name = initialize->typeName;
            while (!name.empty()) {
                lineage.push_back(name);
                name = context.classes.at(name)->baseName;
            }
            for (auto it = lineage.rbegin(); it != lineage.rend(); ++it) {
                const ClassStmt& klass = *context.classes.at(*it);
                for (const auto& field : klass.fields) {
                    object->fields[field.name] = defaultValue(field.type);
                }
            }
            invokeConstructor(object, *context.classes.at(initialize->typeName), {});
        }
        context.types[initialize->name] = ValueType::Object;
        context.classNames[initialize->name] = initialize->typeName;
        context.values[initialize->name] = Value::makeObject(std::move(object));
        return;
    }

    if (auto* instance = dynamic_cast<InstantiateStmt*>(stmt)) {
        auto object = std::make_shared<ObjectInstance>();
        object->className = instance->className;
        std::vector<std::string> lineage;
        std::string name = instance->className;
        while (!name.empty()) {
            lineage.push_back(name);
            name = context.classes.at(name)->baseName;
        }
        for (auto it = lineage.rbegin(); it != lineage.rend(); ++it) {
            const ClassStmt& klass = *context.classes.at(*it);
            for (const auto& field : klass.fields) object->fields[field.name] = defaultValue(field.type);
        }
        std::vector<Value> arguments;
        arguments.reserve(instance->arguments.size());
        for (const auto& argument : instance->arguments) arguments.push_back(eval(argument.get()));
        invokeConstructor(object, *context.classes.at(instance->className), arguments);
        context.types[instance->name] = ValueType::Object;
        context.classNames[instance->name] = instance->className;
        context.values[instance->name] = Value::makeObject(std::move(object));
        return;
    }

    if (auto* assignment = dynamic_cast<AssignmentStmt*>(stmt)) {
        if (context.constants.count(assignment->name)) {
            throw std::runtime_error("cannot assign to constant '" + assignment->name + "'");
        }
        auto typeIt = context.types.find(assignment->name);
        if (typeIt == context.types.end()) {
            if (!activeObject || !activeObject->fields.count(assignment->name)) {
                throw std::runtime_error("variable or field '" + assignment->name + "' has not been initialized");
            }
            const Value old = activeObject->fields.at(assignment->name);
            activeObject->fields[assignment->name] = castTo(old.type, eval(assignment->expr.get()));
            return;
        }
        const ValueType targetType = typeIt->second;
        const Value assigned = castTo(targetType, eval(assignment->expr.get()));
        context.values[assignment->name] = assigned;
        return;
    }

    if (auto* assignment = dynamic_cast<MemberAssignmentStmt*>(stmt)) {
        auto object = context.values.find(assignment->objectName);
        if (object == context.values.end() || object->second.type != ValueType::Object ||
            !object->second.object) {
            throw std::runtime_error("'" + assignment->objectName + "' is not a class instance");
        }
        const auto instance = object->second.object;
        auto field = instance->fields.find(assignment->field);
        if (field == instance->fields.end()) {
            throw std::runtime_error("undefined field '" + assignment->field + "'");
        }
        const ValueType fieldType = field->second.type;
        const Value assigned = castTo(fieldType, eval(assignment->expr.get()));
        field = instance->fields.find(assignment->field);
        field->second = assigned;
        return;
    }

    if (auto* mutation = dynamic_cast<ListMutationStmt*>(stmt)) {
        if (context.constants.count(mutation->name)) {
            throw std::runtime_error("cannot modify list constant '" + mutation->name + "'");
        }
        const Value appended = mutation->value ? eval(mutation->value.get()) : Value{};
        const auto list = context.values.find(mutation->name);
        if (list == context.values.end() || list->second.type != ValueType::List ||
            !list->second.list) {
            throw std::runtime_error("'" + mutation->name + "' is not a list");
        }
        if (mutation->operation == ListMutationStmt::Operation::Append) {
            list->second.list->push_back(appended);
        } else {
            if (list->second.list->empty()) throw std::runtime_error("cannot remove item from an empty list");
            list->second.list->pop_back();
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
        auto object = context.values.find(call->objectName);
        if (object == context.values.end() || object->second.type != ValueType::Object ||
            !object->second.object) {
            throw std::runtime_error("'" + call->objectName + "' is not a class instance");
        }
        std::vector<Value> arguments;
        arguments.reserve(call->arguments.size());
        for (const auto& argument : call->arguments) arguments.push_back(eval(argument.get()));
        invokeMethod(object->second.object, call->method, arguments);
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
        if (context.constants.count(input->name)) {
            throw std::runtime_error("cannot assign input to constant '" + input->name + "'");
        }
        const Value prompt = eval(input->prompt.get());
        std::cout << prompt.toString() << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("failed to read input for '" + input->name + "'");
        }

        ValueType targetType;
        auto variable = context.types.find(input->name);
        const bool isField = variable == context.types.end() && activeObject &&
                             activeObject->fields.count(input->name);
        if (isField) {
            targetType = activeObject->fields.at(input->name).type;
        } else if (variable != context.types.end()) {
            targetType = variable->second;
        } else {
            throw std::runtime_error("ask target '" + input->name + "' is not declared");
        }

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

        if (isField) activeObject->fields[input->name] = std::move(converted);
        else context.values[input->name] = std::move(converted);
        return;
    }

    if (auto* compound = dynamic_cast<AddToStmt*>(stmt)) {
        if (context.constants.count(compound->name)) {
            throw std::runtime_error("cannot modify constant '" + compound->name + "'");
        }
        auto typeIt = context.types.find(compound->name);
        const bool isField = typeIt == context.types.end() && activeObject &&
                             activeObject->fields.count(compound->name);
        if (typeIt == context.types.end() && !isField) {
            throw std::runtime_error("variable or field '" + compound->name + "' has not been initialized");
        }

        const Value current = isField ? activeObject->fields.at(compound->name)
                                      : context.values.at(compound->name);
        const Value amount = eval(compound->amount.get());
        const double a = asDouble(current);
        const double b = asDouble(amount);
        double result = a;

        if (compound->op == "add") result = a + b;
        else if (compound->op == "subtract") result = a - b;
        else if (compound->op == "multiply") result = a * b;
        else if (compound->op == "divide" || compound->op == "mod") {
            if (std::fabs(b) < 1e-15) {
                throw std::runtime_error(compound->op == "divide" ? "division by zero" : "modulo by zero");
            }
            result = compound->op == "divide" ? a / b : std::fmod(a, b);
        }

        Value updated;
        if (current.type == ValueType::Int) {
            updated = Value::makeInt(static_cast<long long>(result));
        } else {
            updated = Value::makeFloat(result);
        }
        if (isField) activeObject->fields[compound->name] = std::move(updated);
        else context.values[compound->name] = std::move(updated);
        return;
    }

    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (truth(eval(ifStmt->condition.get()))) execBlock(ifStmt->thenBlock.get());
        else if (ifStmt->elseBlock) execBlock(ifStmt->elseBlock.get());
        return;
    }

    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        std::size_t guard = 0;
        while (truth(eval(whileStmt->condition.get()))) {
            try {
                execBlock(whileStmt->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            if (++guard > 10000000) throw std::runtime_error("possible infinite loop");
        }
        return;
    }

    if (auto* repeat = dynamic_cast<RepeatStmt*>(stmt)) {
        const Value countValue = eval(repeat->count.get());
        const long long count = countValue.type == ValueType::Int
                                    ? countValue.i
                                    : static_cast<long long>(countValue.f);
        if (count < 0) throw std::runtime_error("repeat count cannot be negative");
        for (long long k = 0; k < count; ++k) {
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
        std::size_t guard = 0;
        while (truth(eval(forStmt->condition.get()))) {
            try {
                execBlock(forStmt->body.get());
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            exec(forStmt->increment.get());
            if (++guard > 10000000) throw std::runtime_error("possible infinite loop");
        }
        return;
    }

    if (auto* each = dynamic_cast<ForEachStmt*>(stmt)) {
        const Value collection = eval(each->collection.get());
        if (collection.type != ValueType::List || !collection.list) {
            throw std::runtime_error("each-in loop requires a list");
        }
        const bool hadType = context.types.count(each->name) != 0;
        const bool hadValue = context.values.count(each->name) != 0;
        const ValueType previousType = hadType ? context.types.at(each->name) : ValueType::Any;
        const Value previousValue = hadValue ? context.values.at(each->name) : Value{};
        context.types[each->name] = ValueType::Any;
        std::size_t guard = 0;
        try {
            const std::vector<Value> items = *collection.list;
            for (const Value& item : items) {
                context.values[each->name] = item;
                try {
                    execBlock(each->body.get());
                } catch (const ContinueSignal&) {
                } catch (const BreakSignal&) {
                    break;
                }
                if (++guard > 10000000) throw std::runtime_error("possible infinite loop");
            }
        } catch (...) {
            if (hadType) context.types[each->name] = previousType;
            else context.types.erase(each->name);
            if (hadValue) context.values[each->name] = previousValue;
            else context.values.erase(each->name);
            throw;
        }
        if (hadType) context.types[each->name] = previousType;
        else context.types.erase(each->name);
        if (hadValue) context.values[each->name] = previousValue;
        else context.values.erase(each->name);
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
    context = {};
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
