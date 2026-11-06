#pragma once

#include "value.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Expr {
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct LiteralExpr : Expr {
  Value value;
  explicit LiteralExpr(Value v) : value(std::move(v)) {
  }
};

struct VariableExpr : Expr {
  std::string name;
  explicit VariableExpr(std::string n) : name(std::move(n)) {
  }
};

struct BinaryExpr : Expr {
  std::string op;
  ExprPtr left;
  ExprPtr right;
  BinaryExpr(std::string o, ExprPtr l, ExprPtr r)
      : op(std::move(o)), left(std::move(l)), right(std::move(r)) {
  }
};

struct UnaryExpr : Expr {
  std::string op;
  ExprPtr expr;
  UnaryExpr(std::string o, ExprPtr e) : op(std::move(o)), expr(std::move(e)) {
  }
};

struct MemberExpr : Expr {
  std::string field;
  ExprPtr object;
  MemberExpr(std::string f, ExprPtr o) : field(std::move(f)), object(std::move(o)) {
  }
};

struct CallExpr : Expr {
  std::string name;
  std::vector<ExprPtr> arguments;
};

struct ListExpr : Expr {
  enum class Operation { Index, Length, Top };
  Operation operation;
  std::string name;
  ExprPtr index;
};

struct Parameter {
  ValueType type;
  std::string name;
  std::string genericType;
};

struct ClassField {
  ValueType type;
  std::string name;
  bool isPublic = true;
};

struct ClassMethod {
  std::string name;
  std::vector<Parameter> parameters;
  ValueType returnType = ValueType::Void;
  bool isPublic = true;
  bool isAbstract = false;
  std::unique_ptr<struct BlockStmt> body;
};

struct ClassStmt;

struct Stmt {
  virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct BlockStmt : Stmt {
  std::vector<StmtPtr> statements;
};

struct DeclarationStmt : Stmt {
  ValueType type;
  std::string name;
  ExprPtr initializer;
  DeclarationStmt(ValueType t, std::string n, ExprPtr e)
      : type(t), name(std::move(n)), initializer(std::move(e)) {
  }
};

struct InitializeStmt : Stmt {
  std::string name;
  ValueType type;
  InitializeStmt(std::string n, ValueType t) : name(std::move(n)), type(t) {
  }
};

struct NamedInitializeStmt : Stmt {
  std::string typeName;
  std::string name;
};

struct AssignmentStmt : Stmt {
  std::string name;
  ExprPtr expr;
  AssignmentStmt(std::string n, ExprPtr e) : name(std::move(n)), expr(std::move(e)) {
  }
};

struct MemberAssignmentStmt : Stmt {
  std::string field;
  std::string objectName;
  ExprPtr expr;
};

struct OutputStmt : Stmt {
  ExprPtr expr;
  explicit OutputStmt(ExprPtr e) : expr(std::move(e)) {
  }
};

struct ExpressionStmt : Stmt {
  ExprPtr expression;
};

struct InputStmt : Stmt {
  ExprPtr prompt;
  std::string name;
};

struct AddToStmt : Stmt {
  std::string op;
  ExprPtr amount;
  std::string name;
  AddToStmt(std::string o, ExprPtr a, std::string n)
      : op(std::move(o)), amount(std::move(a)), name(std::move(n)) {
  }
};

struct IfStmt : Stmt {
  ExprPtr condition;
  std::unique_ptr<BlockStmt> thenBlock;
  std::unique_ptr<BlockStmt> elseBlock;
};

struct WhileStmt : Stmt {
  ExprPtr condition;
  std::unique_ptr<BlockStmt> body;
};

struct RepeatStmt : Stmt {
  ExprPtr count;
  std::unique_ptr<BlockStmt> body;
};

struct ForStmt : Stmt {
  StmtPtr init;
  ExprPtr condition;
  StmtPtr increment;
  std::unique_ptr<BlockStmt> body;
};

struct ClassStmt : Stmt {
  std::string name;
  std::string baseName;
  bool isAbstract = false;
  std::vector<ClassField> fields;
  std::vector<ClassMethod> methods;
  std::vector<Parameter> constructorParameters;
  std::unique_ptr<BlockStmt> constructorBody;
  bool hasConstructor = false;
  bool constructorPublic = false;
};

struct StructStmt : Stmt {
  std::string name;
  std::vector<ClassField> fields;
};

struct EnumStmt : Stmt {
  std::string name;
  std::vector<std::string> members;
};

struct FunctionStmt : Stmt {
  std::string name;
  std::vector<std::string> genericTypeParameters;
  std::vector<Parameter> parameters;
  ValueType returnType = ValueType::Void;
  std::string genericReturnType;
  std::unique_ptr<BlockStmt> body;
};

struct ReturnStmt : Stmt {
  ExprPtr expression;
};

struct FixStmt : Stmt {
  std::string name;
};

struct CheckCase {
  ExprPtr value;
  std::unique_ptr<BlockStmt> body;
};

struct CheckStmt : Stmt {
  ExprPtr value;
  std::vector<CheckCase> cases;
  std::unique_ptr<BlockStmt> otherwise;
};

struct ForEachStmt : Stmt {
  std::string name;
  ExprPtr collection;
  std::unique_ptr<BlockStmt> body;
};

struct LoopControlStmt : Stmt {
  bool isBreak;
};

struct ListMutationStmt : Stmt {
  enum class Operation { Append, RemoveLast };
  Operation operation;
  std::string name;
  ExprPtr value;
};

struct InstantiateStmt : Stmt {
  std::string className;
  std::string name;
  std::vector<ExprPtr> arguments;
};

struct MethodCallStmt : Stmt {
  std::string method;
  std::string objectName;
  std::vector<ExprPtr> arguments;
};

struct SuperConstructorCallStmt : Stmt {
  std::vector<ExprPtr> arguments;
};

using Program = std::vector<StmtPtr>;
