#pragma once

#include "ast.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

// The variables of one scope: the global scope, or one function/method/constructor call.
struct Frame {
  std::unordered_map<std::string, ValueType> types;
  std::unordered_map<std::string, Value> values;
  std::unordered_map<std::string, std::string> classNames;
  std::unordered_set<std::string> constants;
};

struct Context {
  Frame globals;
  std::unordered_map<std::string, const ClassStmt *> classes;
  std::unordered_map<std::string, const StructStmt *> structs;
  std::unordered_map<std::string, const EnumStmt *> enums;
  std::unordered_map<std::string, const FunctionStmt *> functions;
};

class Interpreter {
  Context context;
  // The frame of the running function/method/constructor, or &context.globals at the top level.
  Frame *frame = nullptr;
  std::shared_ptr<ObjectInstance> activeObject;
  std::string activeClass;
  int callDepth = 0;
  std::uintptr_t stackBase = 0;
  std::size_t stackBudget = 4u << 20;

  // Where a name lives: a variable in a frame, or a field of the active object.
  struct Slot {
    Frame *frame = nullptr;
    Value *value = nullptr;
    bool isField = false;
  };
  // Looks a name up in the current frame, then the active object's fields, then globals.
  // A function never sees its caller's variables.
  Slot resolve(const std::string &name);

  static bool numeric(ValueType type);
  double asDouble(const Value &value) const;
  Value eval(const Expr *expr);
  bool truth(const Value &value) const;
  Value castTo(ValueType target, const Value &value, const std::string &name) const;
  Value assignTo(const Slot &slot, const std::string &name, const Value &value) const;
  bool derivesFrom(const std::string &className, const std::string &baseName) const;
  Value defaultValue(ValueType type) const;
  std::shared_ptr<ObjectInstance> newObject(const std::string &className);
  std::shared_ptr<std::vector<Value>> listNamed(const std::string &name);
  void invokeMethod(std::shared_ptr<ObjectInstance> object, const std::string &method,
                    const std::vector<Value> &arguments);
  void invokeConstructor(const std::shared_ptr<ObjectInstance> &object, const ClassStmt &klass,
                         const std::vector<Value> &arguments);
  Value invokeFunction(const FunctionStmt &function, const std::vector<Value> &arguments);
  void runBody(Frame &local, const std::shared_ptr<ObjectInstance> &object,
               const std::string &className, BlockStmt *body, Value *returned = nullptr,
               bool *didReturn = nullptr);
  void declare(const std::string &name, ValueType type, Value value,
               const std::string &className = "");
  void exec(Stmt *stmt);
  void execBlock(BlockStmt *block);
  void loopGuard(std::size_t &iterations) const;

public:
  // How much C++ stack the running program may use before a call is refused.
  void setStackBudget(std::size_t bytes) {
    stackBudget = bytes;
  }
  void run(const Program &program);
};
