#pragma once

#include "ast.hpp"
#include "token.hpp"
#include <string>
#include <vector>

class Parser {
  std::vector<Token> tokens;
  std::size_t current = 0;

  const Token &peek() const;
  const Token &previous() const;
  bool isAtEnd() const;
  bool check(TokenType type) const;
  bool checkWord(const std::string &word) const;
  bool match(TokenType type);
  bool matchWord(const std::string &word);
  const Token &consume(TokenType type, const std::string &message);
  const Token &consumeWord(const std::string &word, const std::string &message);
  [[noreturn]] void errorAtCurrent(const std::string &message) const;
  [[noreturn]] void errorAt(const Token &token, const std::string &message) const;
  void requireName(const Token &token, const std::string &what) const;

  static bool isTypeWord(const std::string &word);
  static ValueType parseTypeWord(const std::string &word);
  void optionalSemicolon();

  StmtPtr parseStatement();
  StmtPtr parseDeclaration(ValueType type);
  StmtPtr parseInitialize();
  StmtPtr parseSet();
  StmtPtr parseOutput(bool coutSyntax);
  StmtPtr parseInput();
  StmtPtr parseCompound(const std::string &op);
  std::unique_ptr<BlockStmt> parseBlock();
  StmtPtr parseIf();
  ExprPtr parseNaturalCondition();
  ExprPtr parseNaturalComparison();
  StmtPtr parseWhile();
  StmtPtr parseRepeat();
  StmtPtr parseFor();
  StmtPtr parseCheck();
  StmtPtr parseFunction();
  StmtPtr parseReturn();
  StmtPtr parseForEach();
  StmtPtr parseListMutation(bool remove);
  StmtPtr parseExpressionStatement();
  StmtPtr parseSuperConstructorCall();
  StmtPtr parseClass(bool abstractClass);
  StmtPtr parseStruct();
  StmtPtr parseEnum();
  StmtPtr parseClassMember(ClassStmt &klass, bool isPublic, bool abstractMethod);
  std::vector<Parameter>
  parseParameters(TokenType closing, const std::vector<std::string> &genericTypeParameters = {});
  StmtPtr parseMethodCall();

  ExprPtr parseExpression();
  ExprPtr parseLogicalOr();
  ExprPtr parseLogicalAnd();
  ExprPtr parseEquality();
  ExprPtr parseComparison();
  ExprPtr parseTerm();
  ExprPtr parseFactor();
  ExprPtr parseUnary();
  ExprPtr parsePrimary();
  std::vector<ExprPtr> parseArguments();

public:
  explicit Parser(std::vector<Token> tokens);
  Program parseProgram();
};
