#pragma once

#include "ast.hpp"
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

    // Names declared in each enclosing nested block (innermost last). Blocks do not
    // create a new scope for naming purposes, so when a block ends its names stay
    // reserved (retiredNames) but can no longer be used: the block may not have run.
    std::vector<std::vector<std::string>> blockScopes;
    std::unordered_set<std::string> retiredNames;
    // Globals visible inside the function body being analysed.
    std::unordered_set<std::string> functionGlobals;

    // Call graph used to check that a function is not called before the globals it uses
    // are declared. Keys describe a function, method or constructor.
    std::string currentCallable;
    mutable std::unordered_map<std::string, std::set<std::string>> callableGlobals;
    mutable std::unordered_map<std::string, std::set<std::string>> callableCalls;
    mutable std::set<std::string> topLevelCalls;

    [[noreturn]] void error(const std::string& message) const;
    [[noreturn]] void undeclared(const std::string& name, const std::string& message) const;
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
    const ClassStmt* constructorClassFrom(const std::string& className) const;
    bool derivesFrom(const std::string& className, const std::string& baseName) const;
    bool isSubtype(const std::string& source, const std::string& target) const;
    void validateAssignmentType(ValueType target, const Expr* expr, const std::string& name) const;
    void validateObjectAssignment(const std::string& name, const Expr* expr) const;
    ValueType inputTargetType(const std::string& name) const;
    void requireList(const std::string& name) const;
    void validateHierarchy(const Program& program);
    void validateClass(const ClassStmt& klass);
    void declareName(const std::string& name, ValueType type, const std::string& hint = "");
    void checkNotField(const std::string& name, const std::string& kind) const;
    void analyzeBlock(BlockStmt* block);
    void noteName(const std::string& name) const;
    void noteCall(const std::string& callable) const;
    void noteMethodCall(const std::string& className, const std::string& method) const;
    void checkGlobalsDeclared(const std::unordered_set<std::string>& available) const;
    void statement(Stmt* stmt);

public:
    void analyze(const Program& program);
};
