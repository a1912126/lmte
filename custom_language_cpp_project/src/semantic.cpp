#include "semantic.hpp"
#include "common.hpp"
#include "optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
std::string functionKey(const std::string &name) {
  return "function '" + name + "'";
}
std::string methodKey(const std::string &className, const std::string &method) {
  return "method '" + method + "' of class '" + className + "'";
}
std::string constructorKey(const std::string &className) {
  return "the constructor of class '" + className + "'";
}

bool isConstantZero(const Expr *expression) {
  // Uses the optimizer's evaluator, so integer expressions use integer division:
  // (1 / 2) is 0, exactly as at run time.
  const auto value = evaluateConstantExpression(expression);
  if (!value)
    return false;
  if (value->type == ValueType::Int)
    return value->i == 0;
  if (value->type == ValueType::Float)
    return std::fabs(value->f) < 1e-15;
  return false;
}
} // namespace

[[noreturn]] void SemanticAnalyzer::error(const std::string &message) const {
  throw CompileError("Semantic error: " + message);
}

[[noreturn]] void SemanticAnalyzer::undeclared(const std::string &name,
                                               const std::string &message) const {
  if (retiredNames.count(name)) {
    error("variable '" + name +
          "' was declared inside a block and cannot be used after "
          "that block ends (the block may not have run); declare it before the block instead");
  }
  error(message);
}

bool SemanticAnalyzer::isNumeric(ValueType type) const {
  return type == ValueType::Int || type == ValueType::Float;
}

bool SemanticAnalyzer::assignable(ValueType target, ValueType source) const {
  if (source == ValueType::Void)
    return false;
  return target == source || target == ValueType::Any || source == ValueType::Any ||
         (target == ValueType::Float && source == ValueType::Int);
}

const ClassField *SemanticAnalyzer::findField(const std::string &className,
                                              const std::string &field,
                                              std::string *declaringClass) const {
  std::string name = className;
  for (std::size_t depth = 0; !name.empty() && depth <= classes.size(); ++depth) {
    auto classIt = classes.find(name);
    if (classIt == classes.end())
      return nullptr;
    for (const auto &candidate : classIt->second->fields) {
      if (candidate.name == field) {
        if (declaringClass)
          *declaringClass = name;
        return &candidate;
      }
    }
    name = classIt->second->baseName;
  }
  return nullptr;
}

const ClassField *SemanticAnalyzer::findObjectField(const std::string &typeName,
                                                    const std::string &field,
                                                    std::string *declaringClass) const {
  if (classes.count(typeName))
    return findField(typeName, field, declaringClass);
  auto structure = structs.find(typeName);
  if (structure == structs.end())
    return nullptr;
  for (const auto &candidate : structure->second->fields) {
    if (candidate.name == field) {
      if (declaringClass)
        *declaringClass = typeName;
      return &candidate;
    }
  }
  return nullptr;
}

const ClassMethod *SemanticAnalyzer::findMethod(const std::string &className,
                                                const std::string &method,
                                                std::string *declaringClass) const {
  std::string name = className;
  for (std::size_t depth = 0; !name.empty() && depth <= classes.size(); ++depth) {
    auto classIt = classes.find(name);
    if (classIt == classes.end())
      return nullptr;
    for (const auto &candidate : classIt->second->methods) {
      if (candidate.name == method) {
        if (declaringClass)
          *declaringClass = name;
        return &candidate;
      }
    }
    name = classIt->second->baseName;
  }
  return nullptr;
}

// The class whose constructor runs when 'className' is constructed: the class itself,
// or its nearest ancestor that defines a constructor. Null if none does.
const ClassStmt *SemanticAnalyzer::constructorClassFrom(const std::string &className) const {
  std::string name = className;
  for (std::size_t depth = 0; !name.empty() && depth <= classes.size(); ++depth) {
    auto classIt = classes.find(name);
    if (classIt == classes.end())
      return nullptr;
    if (classIt->second->hasConstructor)
      return classIt->second;
    name = classIt->second->baseName;
  }
  return nullptr;
}

bool SemanticAnalyzer::derivesFrom(const std::string &className,
                                   const std::string &baseName) const {
  std::string name = className;
  for (std::size_t depth = 0; !name.empty() && depth <= classes.size(); ++depth) {
    if (name == baseName)
      return true;
    auto it = classes.find(name);
    if (it == classes.end())
      return false;
    name = it->second->baseName;
  }
  return false;
}

bool SemanticAnalyzer::isSubtype(const std::string &source, const std::string &target) const {
  return source == target ||
         (classes.count(source) && classes.count(target) && derivesFrom(source, target));
}

void SemanticAnalyzer::noteName(const std::string &name) const {
  if (!currentCallable.empty() && functionGlobals.count(name)) {
    callableGlobals[currentCallable].insert(name);
  }
}

void SemanticAnalyzer::noteCall(const std::string &callable) const {
  if (currentCallable.empty())
    topLevelCalls.insert(callable);
  else
    callableCalls[currentCallable].insert(callable);
}

void SemanticAnalyzer::noteMethodCall(const std::string &className,
                                      const std::string &method) const {
  // Dynamic dispatch: the object may be of the declared class or any subclass.
  std::string owner;
  if (findMethod(className, method, &owner))
    noteCall(methodKey(owner, method));
  for (const auto &[name, definition] : classes) {
    if (name == className || !derivesFrom(name, className))
      continue;
    for (const auto &candidate : definition->methods) {
      if (candidate.name == method)
        noteCall(methodKey(name, method));
    }
  }
}

void SemanticAnalyzer::checkGlobalsDeclared(
    const std::unordered_set<std::string> &available) const {
  for (const auto &call : topLevelCalls) {
    std::set<std::string> visited;
    std::vector<std::string> pending{call};
    while (!pending.empty()) {
      const std::string callable = pending.back();
      pending.pop_back();
      if (!visited.insert(callable).second)
        continue;
      auto globals = callableGlobals.find(callable);
      if (globals != callableGlobals.end()) {
        for (const auto &global : globals->second) {
          if (!available.count(global)) {
            error("global variable '" + global + "' is used by " + callable +
                  " before it is declared; declare '" + global + "' above the first call");
          }
        }
      }
      auto calls = callableCalls.find(callable);
      if (calls != callableCalls.end()) {
        pending.insert(pending.end(), calls->second.begin(), calls->second.end());
      }
    }
  }
}

std::string SemanticAnalyzer::enumTypeOf(const Expr *expr) const {
  if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
    auto declaredEnum = declaredEnums.find(variable->name);
    if (declaredEnum != declaredEnums.end())
      return declaredEnum->second;
    if (declared.count(variable->name))
      return {};
    for (const auto &[enumName, definition] : enums) {
      if (std::find(definition->members.begin(), definition->members.end(), variable->name) !=
          definition->members.end()) {
        return enumName;
      }
    }
  }
  if (auto *literal = dynamic_cast<const LiteralExpr *>(expr)) {
    if (literal->value.type == ValueType::Enum)
      return literal->value.enumType;
  }
  return {};
}

// Checks that 'name' is a list: a list variable, or (inside a class) an accessible list field.
void SemanticAnalyzer::requireList(const std::string &name) const {
  const auto variable = declared.find(name);
  if (variable != declared.end()) {
    if (variable->second != ValueType::List)
      error("'" + name + "' is not a declared list");
    noteName(name);
    return;
  }
  std::string owner;
  const ClassField *field = nullptr;
  if (!currentClass.empty()) {
    field = findField(currentClass, name, &owner);
  }
  if (field && (field->isPublic || owner == currentClass)) {
    if (field->type != ValueType::List)
      error("'" + name + "' is not a declared list");
    return;
  }
  undeclared(name, "'" + name + "' is not a declared list");
}

ValueType SemanticAnalyzer::exprType(const Expr *expr) const {
  if (auto *literal = dynamic_cast<const LiteralExpr *>(expr))
    return literal->value.type;
  if (auto *list = dynamic_cast<const ListExpr *>(expr)) {
    requireList(list->name);
    if (list->operation == ListExpr::Operation::Length)
      return ValueType::Int;
    if (list->operation == ListExpr::Operation::Index &&
        exprType(list->index.get()) != ValueType::Int) {
      error("list index must be an integer");
    }
    return ValueType::Any;
  }
  if (auto *call = dynamic_cast<const CallExpr *>(expr)) {
    const auto function = functions.find(call->name);
    if (function == functions.end())
      error("function '" + call->name + "' is not declared");
    if (function->second->parameters.size() != call->arguments.size()) {
      error("wrong number of arguments for function '" + call->name + "'");
    }
    noteCall(functionKey(call->name));
    std::unordered_map<std::string, ValueType> genericBindings;
    for (std::size_t i = 0; i < call->arguments.size(); ++i) {
      const Parameter &parameter = function->second->parameters[i];
      const ValueType argumentType = exprType(call->arguments[i].get());
      if (argumentType == ValueType::Void) {
        error("cannot pass the result of a nothing-returning function to '" + call->name + "'");
      }
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
        error("cannot infer generic return type '" + function->second->genericReturnType +
              "' for function '" + call->name + "'");
      }
      return binding->second;
    }
    return function->second->returnType;
  }
  if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
    auto it = declared.find(variable->name);
    if (it != declared.end()) {
      noteName(variable->name);
      return it->second;
    }
    std::string owner;
    const ClassField *field = nullptr;
    if (!currentClass.empty()) {
      field = findField(currentClass, variable->name, &owner);
    }
    if (field && (field->isPublic || owner == currentClass))
      return field->type;
    if (!enumTypeOf(variable).empty())
      return ValueType::Enum;
    undeclared(variable->name,
               "variable or field '" + variable->name + "' used before declaration");
  }
  if (auto *member = dynamic_cast<const MemberExpr *>(expr)) {
    auto *objectExpr = dynamic_cast<const VariableExpr *>(member->object.get());
    if (!objectExpr)
      error("field access requires an object variable");
    auto classIt = declaredClasses.find(objectExpr->name);
    if (classIt == declaredClasses.end()) {
      undeclared(objectExpr->name, "'" + objectExpr->name + "' is not a class instance");
    }
    noteName(objectExpr->name);
    std::string owner;
    const ClassField *field = findObjectField(classIt->second, member->field, &owner);
    if (!field)
      error("type '" + classIt->second + "' has no field '" + member->field + "'");
    if (!field->isPublic && owner != currentClass) {
      error("field '" + member->field + "' is private");
    }
    return field->type;
  }
  if (auto *unary = dynamic_cast<const UnaryExpr *>(expr)) {
    ValueType type = exprType(unary->expr.get());
    if (unary->op == "not") {
      if (type != ValueType::Bool && type != ValueType::Any)
        error("'not' requires a boolean expression");
      return ValueType::Bool;
    }
    if (type == ValueType::Any)
      return ValueType::Any;
    if (!isNumeric(type))
      error("unary '" + unary->op + "' requires a numeric expression");
    return type;
  }
  if (auto *binary = dynamic_cast<const BinaryExpr *>(expr)) {
    ValueType left = exprType(binary->left.get());
    ValueType right = exprType(binary->right.get());
    if (left == ValueType::Void || right == ValueType::Void) {
      error("the result of a nothing-returning function cannot be used in an expression");
    }
    if (binary->op == "and" || binary->op == "or") {
      if ((left != ValueType::Bool && left != ValueType::Any) ||
          (right != ValueType::Bool && right != ValueType::Any)) {
        error("logical operators require boolean operands");
      }
      return ValueType::Bool;
    }
    if (binary->op == "+" || binary->op == "-" || binary->op == "*" || binary->op == "/" ||
        binary->op == "mod") {
      if (binary->op == "+" && left == ValueType::String && right == ValueType::String) {
        return ValueType::String;
      }
      if ((!isNumeric(left) && left != ValueType::Any) ||
          (!isNumeric(right) && right != ValueType::Any)) {
        error("operator '" + binary->op + "' requires numeric operands");
      }
      if ((binary->op == "/" || binary->op == "mod") && isConstantZero(binary->right.get())) {
        if (binary->op == "/") {
          error("division by constant zero");
        }
        error("modulo by constant zero");
      }
      if (left == ValueType::Any || right == ValueType::Any)
        return ValueType::Any;
      if (left == ValueType::Float || right == ValueType::Float) {
        return ValueType::Float;
      }
      return ValueType::Int;
    }
    if (binary->op == "==" || binary->op == "!=") {
      if ((isNumeric(left) && isNumeric(right)) || left == right || left == ValueType::Any ||
          right == ValueType::Any) {
        if (left != ValueType::Enum ||
            enumTypeOf(binary->left.get()) == enumTypeOf(binary->right.get())) {
          return ValueType::Bool;
        }
      }
      error("cannot compare " + typeName(left) + " with " + typeName(right));
    }
    if (binary->op == "<" || binary->op == "<=" || binary->op == ">" || binary->op == ">=") {
      if ((isNumeric(left) && isNumeric(right)) ||
          (left == ValueType::String && right == ValueType::String) || left == ValueType::Any ||
          right == ValueType::Any)
        return ValueType::Bool;
      error("comparison requires numeric operands or two strings");
    }
    error("unknown operator '" + binary->op + "'");
  }
  error("invalid expression");
}

std::string SemanticAnalyzer::genericExprType(const Expr *expr) const {
  if (auto *variable = dynamic_cast<const VariableExpr *>(expr)) {
    auto it = declaredGenericTypes.find(variable->name);
    if (it == declaredGenericTypes.end()) {
      return "";
    }
    return it->second;
  }
  if (auto *call = dynamic_cast<const CallExpr *>(expr)) {
    auto function = functions.find(call->name);
    if (function == functions.end() || function->second->genericReturnType.empty())
      return "";
    const std::string &genericReturn = function->second->genericReturnType;
    for (std::size_t i = 0; i < function->second->parameters.size(); ++i) {
      if (function->second->parameters[i].genericType == genericReturn &&
          i < call->arguments.size()) {
        return genericExprType(call->arguments[i].get());
      }
    }
  }
  return "";
}

void SemanticAnalyzer::validateAssignmentType(ValueType target, const Expr *expr,
                                              const std::string &name) const {
  const ValueType source = exprType(expr);
  if (source == ValueType::Void) {
    error("cannot assign the result of a nothing-returning function to '" + name + "'");
  }
  if (target == ValueType::String) {
    // The deliberate string rule (User Guide 5.4): only a quoted literal or another
    // string variable can be assigned to a string.
    const auto *literal = dynamic_cast<const LiteralExpr *>(expr);
    const auto *variable = dynamic_cast<const VariableExpr *>(expr);
    if ((literal && literal->value.type == ValueType::String) ||
        (variable && source == ValueType::String) || source == ValueType::Any)
      return;
    error("string variable '" + name +
          "' must be assigned a quoted string literal or another string variable");
  }
  if (target == ValueType::Enum && enumTypeOf(expr).empty()) {
    error("enum variable '" + name + "' must be assigned one of its named members");
  }
  if (!assignable(target, source)) {
    error("cannot assign " + typeName(source) + " to " + typeName(target) + " variable or field '" +
          name + "'");
  }
}

// An object variable may only hold an object of its declared class or a subclass.
void SemanticAnalyzer::validateObjectAssignment(const std::string &name, const Expr *expr) const {
  const std::string &target = declaredClasses.at(name);
  const ValueType source = exprType(expr);
  if (source == ValueType::Any)
    return; // a list item: checked at run time
  if (source != ValueType::Object) {
    error("cannot assign " + typeName(source) + " to " + target + " variable '" + name + "'");
  }
  const auto *variable = dynamic_cast<const VariableExpr *>(expr);
  auto sourceClass = declaredClasses.end();
  if (variable) {
    sourceClass = declaredClasses.find(variable->name);
  }
  if (sourceClass == declaredClasses.end()) {
    error("cannot assign this value to " + target + " variable '" + name + "'");
  }
  if (!isSubtype(sourceClass->second, target)) {
    std::string message =
        "cannot assign " + sourceClass->second + " to " + target + " variable '" + name + "'";
    if (classes.count(target)) {
      message += " (only " + target + " or a subclass of it is allowed)";
    }
    error(message);
  }
}

ValueType SemanticAnalyzer::inputTargetType(const std::string &name) const {
  auto variable = declared.find(name);
  if (variable != declared.end()) {
    noteName(name);
    return variable->second;
  }

  std::string owner;
  const ClassField *field = nullptr;
  if (!currentClass.empty()) {
    field = findField(currentClass, name, &owner);
  }
  if (field && (field->isPublic || owner == currentClass))
    return field->type;
  undeclared(name, "ask target '" + name + "' must be a declared variable");
}

void SemanticAnalyzer::checkNotField(const std::string &name, const std::string &kind) const {
  if (currentClass.empty())
    return;
  std::string owner;
  if (findField(currentClass, name, &owner)) {
    error(kind + " '" + name + "' has the same name as a field of class '" + owner +
          "', which it would hide; give it a different name");
  }
}

void SemanticAnalyzer::declareName(const std::string &name, ValueType type,
                                   const std::string &hint) {
  if (retiredNames.count(name)) {
    error("variable '" + name +
          "' already declared inside an earlier block; blocks do not "
          "create a new scope, so the name cannot be reused");
  }
  if (declared.count(name)) {
    if (functionGlobals.count(name)) {
      error("variable '" + name +
            "' is already declared as a global variable; a local "
            "variable cannot reuse a global name");
    }
    error("variable '" + name + "' already declared" + hint);
  }
  checkNotField(name, "local variable");
  declared[name] = type;
  if (!blockScopes.empty())
    blockScopes.back().push_back(name);
}

void SemanticAnalyzer::analyzeBlock(BlockStmt *block) {
  if (!block)
    return;
  blockScopes.emplace_back();
  for (auto &child : block->statements)
    statement(child.get());
  const std::vector<std::string> names = std::move(blockScopes.back());
  blockScopes.pop_back();
  for (const auto &name : names) {
    declared.erase(name);
    declaredClasses.erase(name);
    declaredEnums.erase(name);
    fixedVariables.erase(name);
    retiredNames.insert(name);
  }
}

void SemanticAnalyzer::validateHierarchy(const Program &program) {
  // Check every base class exists, then reject cycles, before anything walks the
  // hierarchy (which would otherwise recurse forever or look up a missing class).
  for (const auto &stmt : program) {
    auto *klass = dynamic_cast<const ClassStmt *>(stmt.get());
    if (!klass || klass->baseName.empty() || classes.count(klass->baseName))
      continue;
    if (structs.count(klass->baseName) || enums.count(klass->baseName)) {
      error("class '" + klass->name + "' cannot inherit from '" + klass->baseName +
            "' because it is not a class");
    }
    error("base class '" + klass->baseName + "' is not declared");
  }
  for (const auto &stmt : program) {
    auto *klass = dynamic_cast<const ClassStmt *>(stmt.get());
    if (!klass)
      continue;
    std::unordered_set<std::string> seen;
    for (std::string name = klass->name; !name.empty(); name = classes.at(name)->baseName) {
      if (!seen.insert(name).second)
        error("inheritance cycle involving class '" + name + "'");
    }
  }
}

void SemanticAnalyzer::validateClass(const ClassStmt &klass) {
  std::unordered_map<std::string, bool> fieldNames;
  for (const auto &field : klass.fields) {
    if (!fieldNames.emplace(field.name, true).second ||
        (!klass.baseName.empty() && findField(klass.baseName, field.name))) {
      error("field '" + field.name + "' already exists in class hierarchy");
    }
  }
  std::unordered_map<std::string, bool> methodNames;
  for (const auto &method : klass.methods) {
    if (!methodNames.emplace(method.name, true).second) {
      error("method '" + method.name + "' is declared more than once");
    }
    if (!klass.baseName.empty()) {
      std::string baseOwner;
      const ClassMethod *baseMethod = findMethod(klass.baseName, method.name, &baseOwner);
      if (baseMethod && (baseMethod->returnType != method.returnType ||
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
  for (const auto &[className, definition] : classes) {
    (void)className;
    for (const auto &method : definition->methods) {
      if (method.isAbstract)
        requiredMethods[method.name] = true;
    }
  }
  for (const auto &[methodName, unused] : requiredMethods) {
    (void)unused;
    const ClassMethod *method = findMethod(klass.name, methodName);
    if (!klass.isAbstract && method && method->isAbstract) {
      error("concrete class '" + klass.name + "' must implement abstract method '" + methodName +
            "'");
    }
  }

  if (klass.hasConstructor && !klass.baseName.empty()) {
    const auto &statements = klass.constructorBody->statements;
    const bool explicitSuper =
        !statements.empty() &&
        dynamic_cast<const SuperConstructorCallStmt *>(statements.front().get()) != nullptr;
    const ClassStmt *inherited = constructorClassFrom(klass.baseName);
    // Without super(...), the inherited constructor is run automatically, which is
    // only possible when it takes no arguments.
    if (!explicitSuper && inherited && !inherited->constructorParameters.empty()) {
      error("the constructor of class '" + klass.name +
            "' must call super(...) as its "
            "first statement, because the constructor of class '" +
            inherited->name + "' requires arguments");
    }
  }
  // Constructing a class always runs its ancestors' constructors too.
  if (!klass.baseName.empty()) {
    callableCalls[constructorKey(klass.name)].insert(constructorKey(klass.baseName));
  }

  const auto savedDeclared = declared;
  const auto savedClasses = declaredClasses;
  const auto savedEnums = declaredEnums;
  const auto savedFixedVariables = fixedVariables;
  const auto savedGenericTypes = declaredGenericTypes;
  const auto savedRetired = retiredNames;
  const auto savedBlockScopes = blockScopes;
  const auto savedFunctionGlobals = functionGlobals;
  const std::string savedCallable = currentCallable;
  const std::string savedCurrentClass = currentClass;
  const ValueType savedReturnType = currentReturnType;
  const bool savedInsideFunction = insideFunction;
  const bool savedInsideConstructor = insideConstructor;
  const bool savedSuperCallAllowed = superCallAllowed;
  currentClass = klass.name;
  auto validateBody = [&](const std::vector<Parameter> &parameters, BlockStmt *body,
                          bool isConstructor, const std::string &callable) {
    declared.clear();
    declaredClasses.clear();
    declaredEnums.clear();
    fixedVariables.clear();
    declaredGenericTypes.clear();
    retiredNames.clear();
    blockScopes.clear();
    functionGlobals.clear();
    currentCallable = callable;
    std::unordered_set<std::string> parameterNames;
    for (const auto &parameter : parameters) {
      if (!parameterNames.insert(parameter.name).second) {
        error("parameter '" + parameter.name + "' is declared more than once");
      }
      checkNotField(parameter.name, "parameter");
      declared[parameter.name] = parameter.type;
    }
    currentReturnType = ValueType::Void;
    insideFunction = true;
    insideConstructor = isConstructor;
    superCallAllowed = isConstructor;
    if (body) {
      for (auto &stmt : body->statements)
        statement(stmt.get());
    }
  };
  if (klass.hasConstructor) {
    validateBody(klass.constructorParameters, klass.constructorBody.get(), true,
                 constructorKey(klass.name));
  }
  for (const auto &method : klass.methods) {
    if (!method.isAbstract) {
      validateBody(method.parameters, method.body.get(), false, methodKey(klass.name, method.name));
    }
  }
  declared = savedDeclared;
  declaredClasses = savedClasses;
  declaredEnums = savedEnums;
  fixedVariables = savedFixedVariables;
  declaredGenericTypes = savedGenericTypes;
  retiredNames = savedRetired;
  blockScopes = savedBlockScopes;
  functionGlobals = savedFunctionGlobals;
  currentCallable = savedCallable;
  currentClass = savedCurrentClass;
  currentReturnType = savedReturnType;
  insideFunction = savedInsideFunction;
  insideConstructor = savedInsideConstructor;
  superCallAllowed = savedSuperCallAllowed;
}

void SemanticAnalyzer::statement(Stmt *stmt) {
  if (auto *superCall = dynamic_cast<SuperConstructorCallStmt *>(stmt)) {
    if (!insideConstructor)
      error("'super()' can only be used inside a constructor");
    if (!superCallAllowed) {
      error("'super()' must be the first statement in a constructor");
    }
    superCallAllowed = false;
    auto current = classes.find(currentClass);
    if (current == classes.end() || current->second->baseName.empty()) {
      error("'super()' requires a base class");
    }
    const ClassStmt *constructorClass = constructorClassFrom(current->second->baseName);
    const std::vector<Parameter> *expected = nullptr;
    if (constructorClass) {
      expected = &constructorClass->constructorParameters;
    }
    std::size_t expectedCount = 0;
    if (expected) {
      expectedCount = expected->size();
    }
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
  if (insideConstructor && superCallAllowed)
    superCallAllowed = false;
  if (dynamic_cast<ClassStmt *>(stmt) || dynamic_cast<StructStmt *>(stmt) ||
      dynamic_cast<EnumStmt *>(stmt))
    return;
  if (auto *declaration = dynamic_cast<DeclarationStmt *>(stmt)) {
    // The initializer is checked first: 'int x = x + 1' must not see the new x.
    if (declaration->initializer) {
      validateAssignmentType(declaration->type, declaration->initializer.get(), declaration->name);
    }
    declareName(declaration->name, declaration->type);
    return;
  }
  if (auto *initialize = dynamic_cast<InitializeStmt *>(stmt)) {
    declareName(initialize->name, initialize->type);
    return;
  }
  if (auto *fix = dynamic_cast<FixStmt *>(stmt)) {
    if (!declared.count(fix->name)) {
      undeclared(fix->name, "cannot fix undeclared variable '" + fix->name + "'");
    }
    noteName(fix->name);
    if (!fixedVariables.insert(fix->name).second)
      error("variable '" + fix->name + "' is already fixed");
    return;
  }
  if (dynamic_cast<FunctionStmt *>(stmt))
    return;
  if (auto *returned = dynamic_cast<ReturnStmt *>(stmt)) {
    if (!insideFunction)
      error("'return' can only be used inside a function");
    if (currentReturnType == ValueType::Void) {
      if (returned->expression)
        error("nothing-returning function cannot return a value");
    } else {
      if (!returned->expression)
        error("function must return a value");
      if (!currentGenericReturnType.empty() &&
          genericExprType(returned->expression.get()) != currentGenericReturnType) {
        error("return value must have generic type '" + currentGenericReturnType + "'");
      }
      validateAssignmentType(currentReturnType, returned->expression.get(), "return value");
    }
    return;
  }
  if (auto *control = dynamic_cast<LoopControlStmt *>(stmt)) {
    if (loopDepth == 0)
      error("'stop' and 'skip' can only be used inside a loop");
    (void)control;
    return;
  }
  if (auto *initialize = dynamic_cast<NamedInitializeStmt *>(stmt)) {
    auto classIt = classes.find(initialize->typeName);
    if (classIt != classes.end()) {
      if (classIt->second->isAbstract) {
        error("cannot instantiate abstract class '" + initialize->typeName + "'");
      }
      const ClassStmt *constructorClass = constructorClassFrom(initialize->typeName);
      if (constructorClass) {
        if (!constructorClass->constructorPublic)
          error("class constructor is private");
        if (!constructorClass->constructorParameters.empty()) {
          error("class '" + initialize->typeName + "' requires constructor arguments");
        }
      }
      declareName(initialize->name, ValueType::Object);
      declaredClasses[initialize->name] = initialize->typeName;
      noteCall(constructorKey(initialize->typeName));
      return;
    }
    if (structs.count(initialize->typeName)) {
      declareName(initialize->name, ValueType::Object);
      declaredClasses[initialize->name] = initialize->typeName;
      return;
    }
    auto enumIt = enums.find(initialize->typeName);
    if (enumIt != enums.end()) {
      declareName(initialize->name, ValueType::Enum);
      declaredEnums[initialize->name] = initialize->typeName;
      return;
    }
    error("type '" + initialize->typeName + "' is not declared");
  }
  if (auto *instance = dynamic_cast<InstantiateStmt *>(stmt)) {
    auto classIt = classes.find(instance->className);
    if (classIt == classes.end())
      error("class '" + instance->className + "' is not declared");
    if (classIt->second->isAbstract)
      error("cannot instantiate abstract class '" + instance->className + "'");
    std::vector<Parameter> expected;
    const ClassStmt *constructorClass = constructorClassFrom(instance->className);
    if (constructorClass) {
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
    declareName(instance->name, ValueType::Object);
    declaredClasses[instance->name] = instance->className;
    noteCall(constructorKey(instance->className));
    return;
  }
  if (auto *assignment = dynamic_cast<AssignmentStmt *>(stmt)) {
    auto it = declared.find(assignment->name);
    if (it != declared.end()) {
      noteName(assignment->name);
      if (fixedVariables.count(assignment->name)) {
        error("cannot assign to fixed variable '" + assignment->name + "'");
      }
      auto genericType = declaredGenericTypes.find(assignment->name);
      if (genericType != declaredGenericTypes.end() &&
          genericExprType(assignment->expr.get()) != genericType->second) {
        error("assignment to generic parameter '" + assignment->name + "' must preserve type '" +
              genericType->second + "'");
      }
      if (it->second == ValueType::Enum &&
          enumTypeOf(assignment->expr.get()) != declaredEnums[assignment->name]) {
        error("enum assignment for '" + assignment->name + "' has a different enum type");
      }
      if (it->second == ValueType::Object && declaredClasses.count(assignment->name)) {
        validateObjectAssignment(assignment->name, assignment->expr.get());
        return;
      }
      validateAssignmentType(it->second, assignment->expr.get(), assignment->name);
      return;
    }
    std::string owner;
    const ClassField *field = nullptr;
    if (!currentClass.empty()) {
      field = findField(currentClass, assignment->name, &owner);
    }
    if (!field || (!field->isPublic && owner != currentClass)) {
      undeclared(assignment->name,
                 "variable or field '" + assignment->name + "' is not declared or accessible");
    }
    validateAssignmentType(field->type, assignment->expr.get(), assignment->name);
    return;
  }
  if (auto *memberAssignment = dynamic_cast<MemberAssignmentStmt *>(stmt)) {
    auto object = declaredClasses.find(memberAssignment->objectName);
    if (object == declaredClasses.end()) {
      undeclared(memberAssignment->objectName,
                 "'" + memberAssignment->objectName + "' is not a class instance");
    }
    noteName(memberAssignment->objectName);
    std::string owner;
    const ClassField *field = findObjectField(object->second, memberAssignment->field, &owner);
    if (!field)
      error("type '" + object->second + "' has no field '" + memberAssignment->field + "'");
    if (!field->isPublic && owner != currentClass)
      error("field '" + memberAssignment->field + "' is private");
    validateAssignmentType(field->type, memberAssignment->expr.get(), memberAssignment->field);
    return;
  }
  if (auto *call = dynamic_cast<MethodCallStmt *>(stmt)) {
    auto object = declaredClasses.find(call->objectName);
    if (object == declaredClasses.end()) {
      undeclared(call->objectName, "'" + call->objectName + "' is not a class instance");
    }
    noteName(call->objectName);
    std::string owner;
    const ClassMethod *method = findMethod(object->second, call->method, &owner);
    if (!method)
      error("class '" + object->second + "' has no method '" + call->method + "'");
    if (!method->isPublic && owner != currentClass)
      error("method '" + call->method + "' is private");
    if (method->parameters.size() != call->arguments.size()) {
      error("wrong number of arguments for method '" + call->method + "'");
    }
    for (std::size_t i = 0; i < method->parameters.size(); ++i) {
      if (!assignable(method->parameters[i].type, exprType(call->arguments[i].get()))) {
        error("argument type mismatch for method '" + call->method + "'");
      }
    }
    noteMethodCall(object->second, call->method);
    return;
  }
  if (auto *output = dynamic_cast<OutputStmt *>(stmt)) {
    if (exprType(output->expr.get()) == ValueType::Void) {
      error("cannot output the result of a nothing-returning function");
    }
    return;
  }
  if (auto *expression = dynamic_cast<ExpressionStmt *>(stmt)) {
    exprType(expression->expression.get());
    return;
  }
  if (auto *input = dynamic_cast<InputStmt *>(stmt)) {
    if (fixedVariables.count(input->name)) {
      error("cannot read input into fixed variable '" + input->name + "'");
    }
    if (exprType(input->prompt.get()) != ValueType::String) {
      error("ask prompt must be a string expression");
    }
    const ValueType target = inputTargetType(input->name);
    if (target != ValueType::Int && target != ValueType::Float && target != ValueType::String &&
        target != ValueType::Bool) {
      error("ask target '" + input->name + "' must have type integer, float, string, or bool");
    }
    return;
  }
  if (auto *compound = dynamic_cast<AddToStmt *>(stmt)) {
    if (fixedVariables.count(compound->name))
      error("cannot modify fixed variable '" + compound->name + "'");
    auto it = declared.find(compound->name);
    ValueType target;
    if (it != declared.end()) {
      target = it->second;
      noteName(compound->name);
    } else {
      std::string owner;
      const ClassField *field = nullptr;
      if (!currentClass.empty()) {
        field = findField(currentClass, compound->name, &owner);
      }
      if (!field || (!field->isPublic && owner != currentClass)) {
        undeclared(compound->name,
                   "variable or field '" + compound->name + "' is not declared or accessible");
      }
      target = field->type;
    }
    const ValueType amount = exprType(compound->amount.get());
    if (!isNumeric(target) || !isNumeric(amount)) {
      error("compound arithmetic requires numeric operands");
    }
    // Same rule as assignment: a float result cannot be stored in an int.
    if (target == ValueType::Int && amount == ValueType::Float) {
      error("'" + compound->op + "' with a float amount cannot update int variable '" +
            compound->name + "'; a float cannot be stored in an int");
    }
    if ((compound->op == "divide" || compound->op == "mod") &&
        isConstantZero(compound->amount.get())) {
      if (compound->op == "divide") {
        error("division by constant zero");
      }
      error("modulo by constant zero");
    }
    return;
  }
  if (auto *ifStmt = dynamic_cast<IfStmt *>(stmt)) {
    const ValueType conditionType = exprType(ifStmt->condition.get());
    if (conditionType != ValueType::Bool && conditionType != ValueType::Any)
      error("if condition must be boolean");
    analyzeBlock(ifStmt->thenBlock.get());
    analyzeBlock(ifStmt->elseBlock.get());
    return;
  }
  if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt)) {
    const ValueType conditionType = exprType(whileStmt->condition.get());
    if (conditionType != ValueType::Bool && conditionType != ValueType::Any)
      error("while condition must be boolean");
    ++loopDepth;
    analyzeBlock(whileStmt->body.get());
    --loopDepth;
    return;
  }
  if (auto *repeat = dynamic_cast<RepeatStmt *>(stmt)) {
    if (!isNumeric(exprType(repeat->count.get())))
      error("repeat count must be numeric");
    ++loopDepth;
    analyzeBlock(repeat->body.get());
    --loopDepth;
    return;
  }
  if (auto *forStmt = dynamic_cast<ForStmt *>(stmt)) {
    // The loop counter belongs to the enclosing scope and stays visible after the loop.
    if (auto *declaration = dynamic_cast<DeclarationStmt *>(forStmt->init.get());
        declaration && declared.count(declaration->name) &&
        !functionGlobals.count(declaration->name)) {
      error("variable '" + declaration->name +
            "' already declared; blocks do not create a "
            "new scope, so reuse it in this loop with 'for (" +
            declaration->name + " = ...; ...)'");
    }
    statement(forStmt->init.get());
    const ValueType conditionType = exprType(forStmt->condition.get());
    if (conditionType != ValueType::Bool && conditionType != ValueType::Any)
      error("for-loop condition must be boolean");
    statement(forStmt->increment.get());
    ++loopDepth;
    analyzeBlock(forStmt->body.get());
    --loopDepth;
    return;
  }
  if (auto *each = dynamic_cast<ForEachStmt *>(stmt)) {
    if (exprType(each->collection.get()) != ValueType::List)
      error("each-in loop requires a list");
    if (declared.count(each->name) || retiredNames.count(each->name)) {
      error("iteration variable '" + each->name + "' is already declared");
    }
    checkNotField(each->name, "iteration variable");
    declared[each->name] = ValueType::Any;
    ++loopDepth;
    analyzeBlock(each->body.get());
    --loopDepth;
    declared.erase(each->name);
    return;
  }
  if (auto *mutation = dynamic_cast<ListMutationStmt *>(stmt)) {
    requireList(mutation->name);
    if (fixedVariables.count(mutation->name))
      error("cannot modify fixed list '" + mutation->name + "'");
    if (mutation->value && exprType(mutation->value.get()) == ValueType::Void) {
      error("cannot put the result of a nothing-returning function on a list");
    }
    return;
  }
  if (auto *check = dynamic_cast<CheckStmt *>(stmt)) {
    const ValueType checkedType = exprType(check->value.get());
    if (checkedType != ValueType::Int && checkedType != ValueType::Float &&
        checkedType != ValueType::String && checkedType != ValueType::Bool &&
        checkedType != ValueType::Enum && checkedType != ValueType::Any) {
      error("check value must be an int, float, string, bool or enum value");
    }
    for (auto &item : check->cases) {
      const ValueType caseType = exprType(item.value.get());
      if (!(assignable(checkedType, caseType) && assignable(caseType, checkedType)) ||
          (checkedType == ValueType::Enum && caseType == ValueType::Enum &&
           enumTypeOf(check->value.get()) != enumTypeOf(item.value.get()))) {
        error("check value and when case have incompatible types");
      }
      analyzeBlock(item.body.get());
    }
    analyzeBlock(check->otherwise.get());
    return;
  }
  if (auto *block = dynamic_cast<BlockStmt *>(stmt)) {
    analyzeBlock(block);
    return;
  }
  error("unknown statement");
}

void SemanticAnalyzer::analyze(const Program &program) {
  declared.clear();
  declaredClasses.clear();
  declaredEnums.clear();
  classes.clear();
  structs.clear();
  enums.clear();
  functions.clear();
  fixedVariables.clear();
  declaredGenericTypes.clear();
  retiredNames.clear();
  blockScopes.clear();
  functionGlobals.clear();
  currentCallable.clear();
  callableGlobals.clear();
  callableCalls.clear();
  topLevelCalls.clear();
  currentClass.clear();
  insideConstructor = false;
  superCallAllowed = false;
  for (const auto &stmt : program) {
    if (auto *klass = dynamic_cast<const ClassStmt *>(stmt.get())) {
      if (classes.count(klass->name) || structs.count(klass->name) || enums.count(klass->name)) {
        error("class '" + klass->name + "' is declared more than once");
      }
      classes.emplace(klass->name, klass);
    } else if (auto *structure = dynamic_cast<const StructStmt *>(stmt.get())) {
      if (classes.count(structure->name) || structs.count(structure->name) ||
          enums.count(structure->name)) {
        error("type '" + structure->name + "' is declared more than once");
      }
      structs.emplace(structure->name, structure);
    } else if (auto *enumeration = dynamic_cast<const EnumStmt *>(stmt.get())) {
      if (classes.count(enumeration->name) || structs.count(enumeration->name) ||
          enums.count(enumeration->name)) {
        error("type '" + enumeration->name + "' is declared more than once");
      }
      enums.emplace(enumeration->name, enumeration);
    } else if (auto *function = dynamic_cast<const FunctionStmt *>(stmt.get())) {
      if (functions.count(function->name)) {
        error("function '" + function->name + "' is declared more than once");
      }
      functions.emplace(function->name, function);
    }
  }
  validateHierarchy(program);
  for (const auto &stmt : program) {
    if (auto *function = dynamic_cast<const FunctionStmt *>(stmt.get())) {
      declared.clear();
      declaredClasses.clear();
      declaredEnums.clear();
      fixedVariables.clear();
      declaredGenericTypes.clear();
      retiredNames.clear();
      blockScopes.clear();
      // A function can use any top-level global; checkGlobalsDeclared makes sure it
      // is not called before those globals are declared.
      for (const auto &global : program) {
        if (auto *declaration = dynamic_cast<const DeclarationStmt *>(global.get())) {
          declared[declaration->name] = declaration->type;
        } else if (auto *initialize = dynamic_cast<const InitializeStmt *>(global.get())) {
          declared[initialize->name] = initialize->type;
        } else if (auto *initialize = dynamic_cast<const NamedInitializeStmt *>(global.get())) {
          if (classes.count(initialize->typeName) || structs.count(initialize->typeName)) {
            declared[initialize->name] = ValueType::Object;
            declaredClasses[initialize->name] = initialize->typeName;
          } else if (enums.count(initialize->typeName)) {
            declared[initialize->name] = ValueType::Enum;
            declaredEnums[initialize->name] = initialize->typeName;
          }
        } else if (auto *instance = dynamic_cast<const InstantiateStmt *>(global.get())) {
          declared[instance->name] = ValueType::Object;
          declaredClasses[instance->name] = instance->className;
        } else if (auto *fix = dynamic_cast<const FixStmt *>(global.get())) {
          fixedVariables.insert(fix->name);
        }
      }
      functionGlobals.clear();
      for (const auto &[name, type] : declared) {
        (void)type;
        functionGlobals.insert(name);
      }
      currentCallable = functionKey(function->name);
      currentReturnType = function->returnType;
      currentGenericReturnType = function->genericReturnType;
      insideFunction = true;
      std::unordered_set<std::string> genericNames;
      for (const auto &genericType : function->genericTypeParameters) {
        if (!genericNames.insert(genericType).second) {
          error("generic type parameter '" + genericType + "' is repeated");
        }
        const bool usedByParameter = std::any_of(
            function->parameters.begin(), function->parameters.end(),
            [&](const Parameter &parameter) { return parameter.genericType == genericType; });
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
      for (const auto &parameter : function->parameters) {
        if (!parameterNames.insert(parameter.name).second) {
          error("parameter '" + parameter.name + "' is declared more than once");
        }
        // A parameter belongs to the function's own scope and hides a global
        // of the same name inside this function.
        functionGlobals.erase(parameter.name);
        declaredClasses.erase(parameter.name);
        declaredEnums.erase(parameter.name);
        fixedVariables.erase(parameter.name);
        declared[parameter.name] = parameter.type;
        if (!parameter.genericType.empty()) {
          declaredGenericTypes[parameter.name] = parameter.genericType;
        }
      }
      for (auto &child : function->body->statements)
        statement(child.get());
    }
  }
  declared.clear();
  declaredClasses.clear();
  declaredEnums.clear();
  fixedVariables.clear();
  declaredGenericTypes.clear();
  retiredNames.clear();
  blockScopes.clear();
  functionGlobals.clear();
  currentCallable.clear();
  currentReturnType = ValueType::Void;
  currentGenericReturnType.clear();
  insideFunction = false;

  std::unordered_map<std::string, std::string> enumMemberOwners;
  for (const auto &[enumName, definition] : enums) {
    std::unordered_map<std::string, bool> memberNames;
    for (const auto &member : definition->members) {
      if (!memberNames.emplace(member, true).second) {
        error("enum member '" + member + "' is repeated in '" + enumName + "'");
      }
      if (!enumMemberOwners.emplace(member, enumName).second) {
        error("enum member '" + member + "' is ambiguous across enum definitions");
      }
    }
  }
  for (const auto &stmt : program) {
    if (auto *structure = dynamic_cast<const StructStmt *>(stmt.get())) {
      std::unordered_map<std::string, bool> fieldNames;
      for (const auto &field : structure->fields) {
        if (!fieldNames.emplace(field.name, true).second) {
          error("field '" + field.name + "' is repeated in struct '" + structure->name + "'");
        }
      }
    }
  }
  for (const auto &stmt : program) {
    if (auto *klass = dynamic_cast<const ClassStmt *>(stmt.get()))
      validateClass(*klass);
  }
  for (auto &stmt : program) {
    // Globals declared before this statement: anything a call here uses must be among them.
    std::unordered_set<std::string> available;
    for (const auto &[name, type] : declared) {
      (void)type;
      available.insert(name);
    }
    topLevelCalls.clear();
    statement(stmt.get());
    checkGlobalsDeclared(available);
  }
}
