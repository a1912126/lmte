#pragma once

#include "ast.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>

class SemanticAnalyzer {
    std::unordered_map<std::string, ValueType> declared;
    std::unordered_map<std::string, std::string> declaredClasses;
    std::unordered_map<std::string, std::string> declaredEnums;
    std::unordered_map<std::string, const ClassStmt*> classes;
    std::unordered_map<std::string, const StructStmt*> structs;
    std::unordered_map<std::string, const EnumStmt*> enums;
    std::unordered_map<std::string, const FunctionStmt*> functions;
    std::unordered_set<std::string> fixedVariables;
    std::unordered_map<std::string, std::string> declaredGenericTypes;
    std::string currentClass;
    std::string currentGenericReturnType;
    ValueType currentReturnType = ValueType::Void;
    bool insideFunction = false;
    bool insideConstructor = false;
    bool superCallAllowed = false;
    int loopDepth = 0;

    [[noreturn]] void error(const std::string& message) const;
    bool isNumeric(ValueType type) const;
    bool assignable(ValueType target, ValueType source) const;
    ValueType exprType(const Expr* expr) const;
    std::string genericExprType(const Expr* expr) const;
    std::string enumTypeOf(const Expr* expr) const;
    const ClassField* findObjectField(const std::string& typeName, const std::string& field,
                                      std::string* declaringClass = nullptr) const;
    const ClassField* findField(const std::string& className, const std::string& field,
                                std::string* declaringClass = nullptr) const;
    const ClassMethod* findMethod(const std::string& className, const std::string& method,
                                  std::string* declaringClass = nullptr) const;
    bool derivesFrom(const std::string& className, const std::string& baseName) const;
    void validateAssignmentType(ValueType target, const Expr* expr, const std::string& name) const;
    ValueType inputTargetType(const std::string& name) const;
    void validateClass(const ClassStmt& klass);
    void statement(Stmt* stmt);

public:
    void analyze(const Program& program);
};
