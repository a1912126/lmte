#pragma once

#include "ast.hpp"
#include <string>
#include <unordered_map>

struct Context {
    std::unordered_map<std::string, ValueType> types;
    std::unordered_map<std::string, Value> values;
    std::unordered_map<std::string, std::string> classNames;
    std::unordered_map<std::string, const ClassStmt*> classes;
    std::unordered_map<std::string, const StructStmt*> structs;
    std::unordered_map<std::string, const EnumStmt*> enums;
    std::unordered_map<std::string, const FunctionStmt*> functions;
    std::unordered_map<std::string, bool> constants;
};

class Interpreter {
    Context context;
    std::shared_ptr<ObjectInstance> activeObject;
    std::string activeClass;

    static bool numeric(ValueType type);
    double asDouble(const Value& value) const;
    Value eval(const Expr* expr);
    bool truth(const Value& value) const;
    Value castTo(ValueType target, const Value& value) const;
    const ClassField* findField(const std::string& className, const std::string& field) const;
    const ClassMethod* findMethod(const std::string& className, const std::string& method) const;
    Value defaultValue(ValueType type) const;
    void invokeMethod(std::shared_ptr<ObjectInstance> object, const std::string& method,
                      const std::vector<Value>& arguments);
    void invokeConstructor(const std::shared_ptr<ObjectInstance>& object, const ClassStmt& klass,
                           const std::vector<Value>& arguments);
    Value invokeFunction(const FunctionStmt& function, const std::vector<Value>& arguments);
    void exec(Stmt* stmt);
    void execBlock(BlockStmt* block);

public:
    void run(const Program& program);
};
