#include "llvm_backend.hpp"
#include "common.hpp"

#ifdef PLAINSPEAK_NO_LLVM

// Built without LLVM (see the Makefile): everything except --llvm-ir still works.
std::string generateOptimizedLLVMIR(const Program &) {
  throw CompileError("--llvm-ir is unavailable: this build of customlang was compiled without LLVM "
                     "(install LLVM and run 'make clean && make' to enable it)");
}

#else

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <unordered_map>

namespace {

class LLVMBackend {
  llvm::LLVMContext context;
  llvm::Module module{"plainspeak", context};
  llvm::IRBuilder<> builder{context};
  llvm::Function *mainFunction = nullptr;
  llvm::FunctionCallee printfFunction;
  std::unordered_map<std::string, llvm::AllocaInst *> variables;
  std::unordered_map<std::string, ValueType> variableTypes;

  [[noreturn]] void unsupported(const std::string &feature) const {
    throw CompileError("LLVM backend does not support " + feature);
  }

  llvm::Type *llvmType(ValueType type) {
    switch (type) {
    case ValueType::Int:
      return builder.getInt64Ty();
    case ValueType::Float:
      return builder.getDoubleTy();
    case ValueType::Bool:
      return builder.getInt1Ty();
    case ValueType::String:
      return builder.getPtrTy();
    default:
      unsupported("values of type '" + typeName(type) + "'");
    }
  }

  llvm::AllocaInst *createEntryAlloca(const std::string &name, ValueType type) {
    llvm::IRBuilder<> entryBuilder(&mainFunction->getEntryBlock(),
                                   mainFunction->getEntryBlock().begin());
    return entryBuilder.CreateAlloca(llvmType(type), nullptr, name);
  }

  llvm::Value *globalStringPtr(const std::string &value) {
    llvm::GlobalVariable *global = builder.CreateGlobalString(value, "", 0, &module);
    return builder.CreateInBoundsGEP(global->getValueType(), global,
                                     {builder.getInt32(0), builder.getInt32(0)}, "string.ptr");
  }

  llvm::Value *defaultValue(ValueType type) {
    if (type == ValueType::Int)
      return builder.getInt64(0);
    if (type == ValueType::Float)
      return llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
    if (type == ValueType::Bool)
      return builder.getInt1(false);
    if (type == ValueType::String)
      return globalStringPtr("");
    unsupported("default value for '" + typeName(type) + "'");
  }

  ValueType expressionType(const Expr *expression) const {
    if (auto *literal = dynamic_cast<const LiteralExpr *>(expression))
      return literal->value.type;
    if (auto *variable = dynamic_cast<const VariableExpr *>(expression)) {
      auto it = variableTypes.find(variable->name);
      if (it == variableTypes.end()) {
        throw CompileError("LLVM backend: unknown variable '" + variable->name + "'");
      }
      return it->second;
    }
    if (auto *unary = dynamic_cast<const UnaryExpr *>(expression)) {
      if (unary->op == "not")
        unsupported("logical negation in LLVM IR");
      return expressionType(unary->expr.get());
    }
    if (auto *binary = dynamic_cast<const BinaryExpr *>(expression)) {
      if (binary->op == "and" || binary->op == "or" || binary->op == "mod") {
        unsupported("operator '" + binary->op + "' in LLVM IR");
      }
      if (binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
          binary->op == ">" || binary->op == ">=")
        return ValueType::Bool;
      ValueType left = expressionType(binary->left.get());
      ValueType right = expressionType(binary->right.get());
      if (left == ValueType::String && right == ValueType::String)
        return ValueType::String;
      if (left == ValueType::Float || right == ValueType::Float) {
        return ValueType::Float;
      }
      return ValueType::Int;
    }
    unsupported("this expression");
  }

  llvm::Value *convert(llvm::Value *value, ValueType source, ValueType target) {
    if (source == target)
      return value;
    if (source == ValueType::Int && target == ValueType::Float) {
      return builder.CreateSIToFP(value, builder.getDoubleTy(), "int.to.float");
    }
    unsupported("conversion from " + typeName(source) + " to " + typeName(target));
  }

  void checkDivisionByZero(llvm::Value *divisor, ValueType type) {
    llvm::Value *isZero = nullptr;
    if (type == ValueType::Float) {
      isZero = builder.CreateFCmpOEQ(divisor, llvm::ConstantFP::get(builder.getDoubleTy(), 0.0));
    } else {
      isZero = builder.CreateICmpEQ(divisor, builder.getInt64(0));
    }
    llvm::BasicBlock *errorBlock =
        llvm::BasicBlock::Create(context, "division.error", mainFunction);
    llvm::BasicBlock *continueBlock =
        llvm::BasicBlock::Create(context, "division.continue", mainFunction);
    builder.CreateCondBr(isZero, errorBlock, continueBlock);
    builder.SetInsertPoint(errorBlock);
    llvm::FunctionCallee putsFunction = module.getOrInsertFunction(
        "puts", llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, false));
    llvm::FunctionCallee exitFunction = module.getOrInsertFunction(
        "exit", llvm::FunctionType::get(builder.getVoidTy(), {builder.getInt32Ty()}, false));
    builder.CreateCall(putsFunction, {globalStringPtr("RUNTIME ERROR: division by zero")});
    builder.CreateCall(exitFunction, {builder.getInt32(2)});
    builder.CreateUnreachable();
    builder.SetInsertPoint(continueBlock);
  }

  llvm::Value *emitExpression(const Expr *expression) {
    if (auto *literal = dynamic_cast<const LiteralExpr *>(expression)) {
      if (literal->value.type == ValueType::Int)
        return builder.getInt64(literal->value.i);
      if (literal->value.type == ValueType::Float) {
        return llvm::ConstantFP::get(builder.getDoubleTy(), literal->value.f);
      }
      if (literal->value.type == ValueType::Bool)
        return builder.getInt1(literal->value.b);
      if (literal->value.type == ValueType::String) {
        return globalStringPtr(literal->value.s);
      }
      unsupported("this literal type");
    }

    if (auto *variable = dynamic_cast<const VariableExpr *>(expression)) {
      auto it = variables.find(variable->name);
      if (it == variables.end()) {
        throw CompileError("LLVM backend: variable '" + variable->name +
                           "' is not initialized before use");
      }
      return builder.CreateLoad(llvmType(variableTypes.at(variable->name)), it->second,
                                variable->name + ".value");
    }

    if (auto *unary = dynamic_cast<const UnaryExpr *>(expression)) {
      llvm::Value *operand = emitExpression(unary->expr.get());
      const ValueType type = expressionType(unary->expr.get());
      if (unary->op == "+")
        return operand;
      if (type == ValueType::Int)
        return builder.CreateNeg(operand, "neg");
      if (type == ValueType::Float)
        return builder.CreateFNeg(operand, "fneg");
      unsupported("unary operator '" + unary->op + "'");
    }

    auto *binary = dynamic_cast<const BinaryExpr *>(expression);
    if (!binary)
      unsupported("this expression");
    const ValueType leftType = expressionType(binary->left.get());
    const ValueType rightType = expressionType(binary->right.get());
    llvm::Value *left = emitExpression(binary->left.get());
    llvm::Value *right = emitExpression(binary->right.get());
    const bool floating = leftType == ValueType::Float || rightType == ValueType::Float;
    if (floating && leftType != ValueType::String && rightType != ValueType::String) {
      left = convert(left, leftType, ValueType::Float);
      right = convert(right, rightType, ValueType::Float);
    }

    if (binary->op == "+") {
      if (leftType == ValueType::String || rightType == ValueType::String) {
        unsupported("string concatenation in LLVM IR");
      }
      if (floating) {
        return builder.CreateFAdd(left, right, "fadd");
      }
      return builder.CreateAdd(left, right, "add");
    }
    if (binary->op == "-") {
      if (floating) {
        return builder.CreateFSub(left, right, "fsub");
      }
      return builder.CreateSub(left, right, "sub");
    }
    if (binary->op == "*") {
      if (floating) {
        return builder.CreateFMul(left, right, "fmul");
      }
      return builder.CreateMul(left, right, "mul");
    }
    if (binary->op == "/") {
      ValueType divisionType = ValueType::Int;
      if (floating) {
        divisionType = ValueType::Float;
      }
      checkDivisionByZero(right, divisionType);
      if (floating) {
        return builder.CreateFDiv(left, right, "fdiv");
      }
      return builder.CreateSDiv(left, right, "sdiv");
    }

    if (leftType == ValueType::String && rightType == ValueType::String) {
      unsupported("string comparisons in LLVM IR");
    }
    if (floating) {
      llvm::CmpInst::Predicate predicate;
      if (binary->op == "==")
        predicate = llvm::CmpInst::FCMP_OEQ;
      else if (binary->op == "!=")
        predicate = llvm::CmpInst::FCMP_ONE;
      else if (binary->op == "<")
        predicate = llvm::CmpInst::FCMP_OLT;
      else if (binary->op == "<=")
        predicate = llvm::CmpInst::FCMP_OLE;
      else if (binary->op == ">")
        predicate = llvm::CmpInst::FCMP_OGT;
      else if (binary->op == ">=")
        predicate = llvm::CmpInst::FCMP_OGE;
      else
        unsupported("comparison operator '" + binary->op + "'");
      return builder.CreateFCmp(predicate, left, right, "fcmp");
    }

    llvm::CmpInst::Predicate predicate;
    if (binary->op == "==")
      predicate = llvm::CmpInst::ICMP_EQ;
    else if (binary->op == "!=")
      predicate = llvm::CmpInst::ICMP_NE;
    else if (binary->op == "<")
      predicate = llvm::CmpInst::ICMP_SLT;
    else if (binary->op == "<=")
      predicate = llvm::CmpInst::ICMP_SLE;
    else if (binary->op == ">")
      predicate = llvm::CmpInst::ICMP_SGT;
    else if (binary->op == ">=")
      predicate = llvm::CmpInst::ICMP_SGE;
    else
      unsupported("comparison operator '" + binary->op + "'");
    return builder.CreateICmp(predicate, left, right, "icmp");
  }

  void emitOutput(const OutputStmt &output) {
    const ValueType type = expressionType(output.expr.get());
    llvm::Value *value = emitExpression(output.expr.get());
    llvm::Value *format = nullptr;
    if (type == ValueType::Int)
      format = globalStringPtr("%lld\n");
    else if (type == ValueType::Float)
      format = globalStringPtr("%g\n");
    else if (type == ValueType::String)
      format = globalStringPtr("%s\n");
    else if (type == ValueType::Bool) {
      llvm::Value *trueString = globalStringPtr("true\n");
      llvm::Value *falseString = globalStringPtr("false\n");
      value = builder.CreateSelect(value, trueString, falseString, "bool.string");
      format = globalStringPtr("%s");
    } else {
      unsupported("output of type '" + typeName(type) + "'");
    }
    builder.CreateCall(printfFunction, {format, value});
  }

  void emitBlock(const BlockStmt &block) {
    for (const auto &statement : block.statements) {
      if (builder.GetInsertBlock()->hasTerminator())
        break;
      emitStatement(statement.get());
    }
  }

  void emitIf(const IfStmt &statement) {
    llvm::BasicBlock *thenBlock = llvm::BasicBlock::Create(context, "if.then", mainFunction);
    llvm::BasicBlock *mergeBlock = llvm::BasicBlock::Create(context, "if.end", mainFunction);
    llvm::BasicBlock *elseBlock = mergeBlock;
    if (statement.elseBlock) {
      elseBlock = llvm::BasicBlock::Create(context, "if.else", mainFunction);
    }
    builder.CreateCondBr(emitExpression(statement.condition.get()), thenBlock, elseBlock);

    builder.SetInsertPoint(thenBlock);
    emitBlock(*statement.thenBlock);
    if (!builder.GetInsertBlock()->hasTerminator())
      builder.CreateBr(mergeBlock);

    if (statement.elseBlock) {
      builder.SetInsertPoint(elseBlock);
      emitBlock(*statement.elseBlock);
      if (!builder.GetInsertBlock()->hasTerminator())
        builder.CreateBr(mergeBlock);
    }
    builder.SetInsertPoint(mergeBlock);
  }

  void emitWhile(const WhileStmt &statement) {
    llvm::BasicBlock *conditionBlock =
        llvm::BasicBlock::Create(context, "while.cond", mainFunction);
    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(context, "while.body", mainFunction);
    llvm::BasicBlock *endBlock = llvm::BasicBlock::Create(context, "while.end", mainFunction);
    builder.CreateBr(conditionBlock);
    builder.SetInsertPoint(conditionBlock);
    builder.CreateCondBr(emitExpression(statement.condition.get()), bodyBlock, endBlock);
    builder.SetInsertPoint(bodyBlock);
    emitBlock(*statement.body);
    if (!builder.GetInsertBlock()->hasTerminator())
      builder.CreateBr(conditionBlock);
    builder.SetInsertPoint(endBlock);
  }

  void emitRepeat(const RepeatStmt &statement) {
    const ValueType countType = expressionType(statement.count.get());
    llvm::Value *count = emitExpression(statement.count.get());
    if (countType == ValueType::Float)
      count = builder.CreateFPToSI(count, builder.getInt64Ty());
    llvm::AllocaInst *counter = createEntryAlloca("repeat.counter", ValueType::Int);
    builder.CreateStore(builder.getInt64(0), counter);

    llvm::BasicBlock *conditionBlock =
        llvm::BasicBlock::Create(context, "repeat.cond", mainFunction);
    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(context, "repeat.body", mainFunction);
    llvm::BasicBlock *endBlock = llvm::BasicBlock::Create(context, "repeat.end", mainFunction);
    builder.CreateBr(conditionBlock);
    builder.SetInsertPoint(conditionBlock);
    llvm::Value *index = builder.CreateLoad(builder.getInt64Ty(), counter);
    builder.CreateCondBr(builder.CreateICmpSLT(index, count), bodyBlock, endBlock);
    builder.SetInsertPoint(bodyBlock);
    emitBlock(*statement.body);
    if (!builder.GetInsertBlock()->hasTerminator()) {
      llvm::Value *next =
          builder.CreateAdd(builder.CreateLoad(builder.getInt64Ty(), counter), builder.getInt64(1));
      builder.CreateStore(next, counter);
      builder.CreateBr(conditionBlock);
    }
    builder.SetInsertPoint(endBlock);
  }

  void emitFor(const ForStmt &statement) {
    emitStatement(statement.init.get());
    llvm::BasicBlock *conditionBlock = llvm::BasicBlock::Create(context, "for.cond", mainFunction);
    llvm::BasicBlock *bodyBlock = llvm::BasicBlock::Create(context, "for.body", mainFunction);
    llvm::BasicBlock *incrementBlock =
        llvm::BasicBlock::Create(context, "for.increment", mainFunction);
    llvm::BasicBlock *endBlock = llvm::BasicBlock::Create(context, "for.end", mainFunction);
    builder.CreateBr(conditionBlock);
    builder.SetInsertPoint(conditionBlock);
    builder.CreateCondBr(emitExpression(statement.condition.get()), bodyBlock, endBlock);
    builder.SetInsertPoint(bodyBlock);
    emitBlock(*statement.body);
    if (!builder.GetInsertBlock()->hasTerminator())
      builder.CreateBr(incrementBlock);
    builder.SetInsertPoint(incrementBlock);
    emitStatement(statement.increment.get());
    if (!builder.GetInsertBlock()->hasTerminator())
      builder.CreateBr(conditionBlock);
    builder.SetInsertPoint(endBlock);
  }

  void emitStatement(const Stmt *statement) {
    if (dynamic_cast<const ClassStmt *>(statement)) {
      unsupported("class declarations");
    }
    if (dynamic_cast<const StructStmt *>(statement)) {
      unsupported("struct declarations");
    }
    if (dynamic_cast<const EnumStmt *>(statement)) {
      unsupported("enum declarations");
    }
    if (dynamic_cast<const NamedInitializeStmt *>(statement) ||
        dynamic_cast<const InstantiateStmt *>(statement)) {
      unsupported("class, struct, or enum instances");
    }
    if (dynamic_cast<const InputStmt *>(statement)) {
      unsupported("input statements");
    }
    if (dynamic_cast<const MemberAssignmentStmt *>(statement) ||
        dynamic_cast<const MethodCallStmt *>(statement)) {
      unsupported("object member operations");
    }
    if (auto *declaration = dynamic_cast<const DeclarationStmt *>(statement)) {
      if (variables.count(declaration->name)) {
        throw CompileError("LLVM backend: duplicate variable '" + declaration->name + "'");
      }
      variableTypes[declaration->name] = declaration->type;
      variables[declaration->name] = createEntryAlloca(declaration->name, declaration->type);
      llvm::Value *initial = nullptr;
      if (declaration->initializer) {
        initial = convert(emitExpression(declaration->initializer.get()),
                          expressionType(declaration->initializer.get()), declaration->type);
      } else {
        initial = defaultValue(declaration->type);
      }
      builder.CreateStore(initial, variables.at(declaration->name));
      return;
    }
    if (auto *initialize = dynamic_cast<const InitializeStmt *>(statement)) {
      variableTypes[initialize->name] = initialize->type;
      variables[initialize->name] = createEntryAlloca(initialize->name, initialize->type);
      builder.CreateStore(defaultValue(initialize->type), variables.at(initialize->name));
      return;
    }
    if (auto *assignment = dynamic_cast<const AssignmentStmt *>(statement)) {
      auto it = variables.find(assignment->name);
      if (it == variables.end()) {
        throw CompileError("LLVM backend: variable '" + assignment->name +
                           "' is not initialized before assignment");
      }
      const ValueType target = variableTypes.at(assignment->name);
      builder.CreateStore(convert(emitExpression(assignment->expr.get()),
                                  expressionType(assignment->expr.get()), target),
                          it->second);
      return;
    }
    if (auto *output = dynamic_cast<const OutputStmt *>(statement)) {
      emitOutput(*output);
      return;
    }
    if (auto *compound = dynamic_cast<const AddToStmt *>(statement)) {
      auto it = variables.find(compound->name);
      if (it == variables.end()) {
        throw CompileError("LLVM backend: variable '" + compound->name + "' is not declared");
      }
      const ValueType type = variableTypes.at(compound->name);
      llvm::Value *current = builder.CreateLoad(llvmType(type), it->second);
      llvm::Value *amount = emitExpression(compound->amount.get());
      const bool floating =
          type == ValueType::Float || expressionType(compound->amount.get()) == ValueType::Float;
      if (floating) {
        if (type != ValueType::Float)
          current = convert(current, type, ValueType::Float);
        amount = convert(amount, expressionType(compound->amount.get()), ValueType::Float);
      }
      if (compound->op == "divide") {
        ValueType divisionType = ValueType::Int;
        if (floating) {
          divisionType = ValueType::Float;
        }
        checkDivisionByZero(amount, divisionType);
      }
      llvm::Value *result = nullptr;
      if (compound->op == "add") {
        if (floating) {
          result = builder.CreateFAdd(current, amount);
        } else {
          result = builder.CreateAdd(current, amount);
        }
      } else if (compound->op == "subtract") {
        if (floating) {
          result = builder.CreateFSub(current, amount);
        } else {
          result = builder.CreateSub(current, amount);
        }
      } else if (compound->op == "multiply") {
        if (floating) {
          result = builder.CreateFMul(current, amount);
        } else {
          result = builder.CreateMul(current, amount);
        }
      } else if (compound->op == "divide") {
        if (floating) {
          result = builder.CreateFDiv(current, amount);
        } else {
          result = builder.CreateSDiv(current, amount);
        }
      } else {
        unsupported("compound operator '" + compound->op + "'");
      }
      if (type == ValueType::Int && floating) {
        result = builder.CreateFPToSI(result, builder.getInt64Ty(), "result.to.int");
      }
      builder.CreateStore(result, it->second);
      return;
    }
    if (auto *ifStatement = dynamic_cast<const IfStmt *>(statement)) {
      emitIf(*ifStatement);
      return;
    }
    if (auto *whileStatement = dynamic_cast<const WhileStmt *>(statement)) {
      emitWhile(*whileStatement);
      return;
    }
    if (auto *repeat = dynamic_cast<const RepeatStmt *>(statement)) {
      emitRepeat(*repeat);
      return;
    }
    if (auto *forStatement = dynamic_cast<const ForStmt *>(statement)) {
      emitFor(*forStatement);
      return;
    }
    unsupported("this statement");
  }

public:
  std::string generate(const Program &program) {
    auto *functionType = llvm::FunctionType::get(builder.getInt32Ty(), false);
    mainFunction =
        llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, "main", module);
    auto *entry = llvm::BasicBlock::Create(context, "entry", mainFunction);
    builder.SetInsertPoint(entry);

    auto *printfType = llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, true);
    printfFunction = module.getOrInsertFunction("printf", printfType);

    for (const auto &statement : program) {
      emitStatement(statement.get());
    }
    builder.CreateRet(builder.getInt32(0));

    std::string verification;
    llvm::raw_string_ostream verificationStream(verification);
    if (llvm::verifyModule(module, &verificationStream)) {
      verificationStream.flush();
      throw CompileError("LLVM module verification failed: " + verification);
    }

    llvm::LoopAnalysisManager loopAnalyses;
    llvm::FunctionAnalysisManager functionAnalyses;
    llvm::CGSCCAnalysisManager cgsccAnalyses;
    llvm::ModuleAnalysisManager moduleAnalyses;
    llvm::PassBuilder passBuilder;
    passBuilder.registerModuleAnalyses(moduleAnalyses);
    passBuilder.registerCGSCCAnalyses(cgsccAnalyses);
    passBuilder.registerFunctionAnalyses(functionAnalyses);
    passBuilder.registerLoopAnalyses(loopAnalyses);
    passBuilder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses, moduleAnalyses);
    llvm::ModulePassManager optimizationPipeline =
        passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    optimizationPipeline.run(module, moduleAnalyses);

    std::string output;
    llvm::raw_string_ostream outputStream(output);
    module.print(outputStream, nullptr);
    outputStream.flush();
    return output;
  }
};

} // namespace

std::string generateOptimizedLLVMIR(const Program &program) {
  LLVMBackend backend;
  return backend.generate(program);
}

#endif // PLAINSPEAK_NO_LLVM
