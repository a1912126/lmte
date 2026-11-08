#include "parser.hpp"
#include "common.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace {
// Words with a fixed meaning in Plainspeak. They cannot be used as the name of a
// variable, parameter, field, function, method, type or enum member.
const std::unordered_set<std::string> &reservedWords() {
  static const std::unordered_set<std::string> words = {
      // types and literals
      "int",
      "integer",
      "float",
      "string",
      "bool",
      "list",
      "nothing",
      "true",
      "false",
      // operators
      "and",
      "or",
      "not",
      "mod",
      "is",
      "of",
      // statements
      "initialize",
      "Initialize",
      "define",
      "Define",
      "abstract",
      "class",
      "struct",
      "enum",
      "function",
      "call",
      "super",
      "set",
      "say",
      "ask",
      "add",
      "subtract",
      "multiply",
      "divide",
      "put",
      "remove",
      "fix",
      "return",
      "stop",
      "skip",
      "if",
      "else",
      "otherwise",
      "check",
      "when",
      "while",
      "repeat",
      "for",
      "each",
      // list queries
      "item",
      "length",
      "top",
      // class members
      "public",
      "private",
      "inherits",
      "constructor",
      "returning",
  };
  return words;
}
} // namespace

void Parser::requireName(const Token &token, const std::string &what) const {
  if (reservedWords().count(token.text)) {
    errorAt(token,
            "'" + token.text + "' is a reserved word and cannot be used as a " + what + " name");
  }
}

Parser::Parser(std::vector<Token> t) : tokens(std::move(t)) {
}

const Token &Parser::peek() const {
  return tokens[current];
}
const Token &Parser::previous() const {
  return tokens[current - 1];
}
bool Parser::isAtEnd() const {
  return peek().type == TokenType::End;
}
bool Parser::check(TokenType t) const {
  return peek().type == t;
}
bool Parser::checkWord(const std::string &word) const {
  return peek().type == TokenType::Identifier && peek().text == word;
}

bool Parser::match(TokenType t) {
  if (!check(t))
    return false;
  ++current;
  return true;
}

bool Parser::matchWord(const std::string &word) {
  if (!checkWord(word))
    return false;
  ++current;
  return true;
}

const Token &Parser::consume(TokenType t, const std::string &message) {
  if (check(t))
    return tokens[current++];
  errorAtCurrent(message + ", found '" + tokenName(peek().type) + "'");
}

const Token &Parser::consumeWord(const std::string &word, const std::string &message) {
  if (checkWord(word))
    return tokens[current++];
  errorAtCurrent(message + ", found '" + peek().text + "'");
}

[[noreturn]] void Parser::errorAtCurrent(const std::string &message) const {
  throw CompileError("Line " + std::to_string(peek().pos.line) + ", column " +
                     std::to_string(peek().pos.column) + ": " + message);
}

[[noreturn]] void Parser::errorAt(const Token &token, const std::string &message) const {
  throw CompileError("Line " + std::to_string(token.pos.line) + ", column " +
                     std::to_string(token.pos.column) + ": " + message);
}

bool Parser::isTypeWord(const std::string &s) {
  return s == "int" || s == "integer" || s == "float" || s == "string" || s == "bool" ||
         s == "list";
}

ValueType Parser::parseTypeWord(const std::string &s) {
  if (s == "int" || s == "integer")
    return ValueType::Int;
  if (s == "float")
    return ValueType::Float;
  if (s == "string")
    return ValueType::String;
  if (s == "bool")
    return ValueType::Bool;
  if (s == "list")
    return ValueType::List;
  if (s == "nothing")
    return ValueType::Void;
  throw std::runtime_error("not a type");
}

StmtPtr Parser::parseStatement() {
  if (matchWord("int"))
    return parseDeclaration(ValueType::Int);
  if (matchWord("integer"))
    return parseDeclaration(ValueType::Int);
  if (matchWord("float"))
    return parseDeclaration(ValueType::Float);
  if (matchWord("string"))
    return parseDeclaration(ValueType::String);
  if (matchWord("bool"))
    return parseDeclaration(ValueType::Bool);
  if (matchWord("list"))
    return parseDeclaration(ValueType::List);
  if (matchWord("initialize"))
    return parseInitialize();
  if (matchWord("Initialize"))
    return parseInitialize();
  if (matchWord("abstract")) {
    consumeWord("define", "expected 'define' after 'abstract'");
    consumeWord("class", "expected 'class' after 'abstract define'");
    return parseClass(true);
  }
  if (matchWord("define")) {
    if (matchWord("class"))
      return parseClass(false);
    if (matchWord("struct"))
      return parseStruct();
    if (matchWord("enum"))
      return parseEnum();
    if (matchWord("function"))
      return parseFunction();
    errorAtCurrent("expected 'class', 'struct', 'enum', or 'function' after 'define'");
  }
  if (matchWord("Define")) {
    if (matchWord("class"))
      return parseClass(false);
    if (matchWord("struct"))
      return parseStruct();
    if (matchWord("enum"))
      return parseEnum();
    if (matchWord("function"))
      return parseFunction();
    errorAtCurrent("expected 'class', 'struct', 'enum', or 'function' after 'Define'");
  }
  if (matchWord("call"))
    return parseMethodCall();
  if (matchWord("super"))
    return parseSuperConstructorCall();
  if (matchWord("set"))
    return parseSet();
  if (matchWord("say"))
    return parseOutput();
  if (matchWord("ask"))
    return parseInput();
  if (matchWord("add"))
    return parseCompound("add");
  if (matchWord("subtract"))
    return parseCompound("subtract");
  if (matchWord("multiply"))
    return parseCompound("multiply");
  if (matchWord("divide"))
    return parseCompound("divide");
  if (matchWord("mod"))
    return parseCompound("mod");
  if (matchWord("put"))
    return parseListMutation(false);
  if (matchWord("remove"))
    return parseListMutation(true);
  if (matchWord("fix")) {
    auto node = std::make_unique<FixStmt>();
    node->name = consume(TokenType::Identifier, "expected variable name after 'fix'").text;
    return node;
  }
  if (matchWord("return"))
    return parseReturn();
  if (matchWord("stop") || matchWord("skip")) {
    auto node = std::make_unique<LoopControlStmt>();
    node->isBreak = previous().text == "stop";
    return node;
  }
  if (matchWord("if"))
    return parseIf();
  if (matchWord("check"))
    return parseCheck();
  if (matchWord("while"))
    return parseWhile();
  if (matchWord("repeat"))
    return parseRepeat();
  if (matchWord("for")) {
    if (matchWord("each"))
      return parseForEach();
    return parseFor();
  }

  if (check(TokenType::Identifier)) {
    if (peek().text != "of" && current + 1 < tokens.size() &&
        tokens[current + 1].type == TokenType::LeftParen) {
      return parseExpressionStatement();
    }
    Token name = peek();
    ++current;
    if (match(TokenType::Assign)) {
      ExprPtr expr = parseExpression();
      return std::make_unique<AssignmentStmt>(name.text, std::move(expr));
    }
    errorAt(name, "expected assignment operator '=' after variable name");
  }

  errorAtCurrent("unexpected token '" + peek().text + "'");
}

StmtPtr Parser::parseDeclaration(ValueType type) {
  Token name = consume(TokenType::Identifier, "expected variable name");
  requireName(name, "variable");
  ExprPtr initializer;
  if (match(TokenType::Assign))
    initializer = parseExpression();
  return std::make_unique<DeclarationStmt>(type, name.text, std::move(initializer));
}

StmtPtr Parser::parseInitialize() {
  Token first = consume(TokenType::Identifier, "expected type or variable name after 'initialize'");
  if (isTypeWord(first.text)) {
    Token variable = consume(TokenType::Identifier, "expected variable name after type");
    requireName(variable, "variable");
    return std::make_unique<InitializeStmt>(variable.text, parseTypeWord(first.text));
  }

  Token second = consume(TokenType::Identifier, "expected variable type or instance name");
  if (isTypeWord(second.text)) {
    requireName(first, "variable");
    return std::make_unique<InitializeStmt>(first.text, parseTypeWord(second.text));
  }
  requireName(second, "variable");
  if (matchWord("with")) {
    consume(TokenType::LeftParen, "expected '(' before constructor arguments");
    auto node = std::make_unique<InstantiateStmt>();
    node->className = first.text;
    node->name = second.text;
    if (!check(TokenType::RightParen)) {
      do {
        node->arguments.push_back(parseExpression());
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "expected ')' after constructor arguments");
    return node;
  }
  auto node = std::make_unique<NamedInitializeStmt>();
  node->typeName = first.text;
  node->name = second.text;
  return node;
}

std::vector<Parameter>
Parser::parseParameters(TokenType closing, const std::vector<std::string> &genericTypeParameters) {
  std::vector<Parameter> parameters;
  if (check(closing))
    return parameters;
  do {
    Token type = consume(TokenType::Identifier, "expected parameter type");
    const bool isGeneric = std::find(genericTypeParameters.begin(), genericTypeParameters.end(),
                                     type.text) != genericTypeParameters.end();
    if (!isTypeWord(type.text) && !isGeneric) {
      errorAt(type, "unsupported parameter type '" + type.text + "'");
    }
    Token name = consume(TokenType::Identifier, "expected parameter name");
    requireName(name, "parameter");
    ValueType parameterType = ValueType::Any;
    std::string genericType;
    if (isGeneric) {
      genericType = type.text;
    } else {
      parameterType = parseTypeWord(type.text);
    }
    parameters.push_back({parameterType, name.text, genericType});
  } while (match(TokenType::Comma));
  return parameters;
}

StmtPtr Parser::parseClass(bool abstractClass) {
  Token name = consume(TokenType::Identifier, "expected class name");
  requireName(name, "class");
  auto klass = std::make_unique<ClassStmt>();
  klass->name = name.text;
  klass->isAbstract = abstractClass;
  if (matchWord("inherits")) {
    klass->baseName = consume(TokenType::Identifier, "expected base class name").text;
  }
  consume(TokenType::LeftBrace, "expected '{' after class declaration");
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    bool isPublic = false;
    bool abstractMethod = false;
    if (matchWord("public"))
      isPublic = true;
    else if (matchWord("private"))
      isPublic = false;
    if (matchWord("abstract"))
      abstractMethod = true;
    parseClassMember(*klass, isPublic, abstractMethod);
  }
  consume(TokenType::RightBrace, "expected '}' after class body");
  return klass;
}

StmtPtr Parser::parseFunction() {
  Token name = consume(TokenType::Identifier, "expected function name");
  requireName(name, "function");
  auto function = std::make_unique<FunctionStmt>();
  function->name = name.text;
  if (match(TokenType::Less)) {
    do {
      Token typeParameter = consume(TokenType::Identifier, "expected generic type parameter");
      requireName(typeParameter, "generic type parameter");
      if (isTypeWord(typeParameter.text) || typeParameter.text == "nothing") {
        errorAt(typeParameter, "generic type parameter must use a distinct name");
      }
      if (std::find(function->genericTypeParameters.begin(), function->genericTypeParameters.end(),
                    typeParameter.text) != function->genericTypeParameters.end()) {
        errorAt(typeParameter, "generic type parameter is repeated");
      }
      function->genericTypeParameters.push_back(typeParameter.text);
    } while (match(TokenType::Comma));
    consume(TokenType::Greater, "expected '>' after generic type parameters");
    if (function->genericTypeParameters.empty()) {
      errorAt(name, "generic function must declare at least one type parameter");
    }
  }
  consume(TokenType::LeftParen, "expected '(' after function name");
  function->parameters = parseParameters(TokenType::RightParen, function->genericTypeParameters);
  consume(TokenType::RightParen, "expected ')' after function parameters");
  consumeWord("returning", "expected 'returning' after function signature");
  Token returnType = consume(TokenType::Identifier, "expected function return type");
  if (returnType.text == "nothing")
    function->returnType = ValueType::Void;
  else if (isTypeWord(returnType.text))
    function->returnType = parseTypeWord(returnType.text);
  else if (std::find(function->genericTypeParameters.begin(), function->genericTypeParameters.end(),
                     returnType.text) != function->genericTypeParameters.end()) {
    function->returnType = ValueType::Any;
    function->genericReturnType = returnType.text;
  } else {
    errorAt(returnType, "unsupported function return type '" + returnType.text + "'");
  }
  function->body = parseBlock();
  return function;
}

StmtPtr Parser::parseReturn() {
  auto result = std::make_unique<ReturnStmt>();
  if (!check(TokenType::RightBrace) && !isAtEnd() &&
      peek().pos.line == previous().pos.line) {
    result->expression = parseExpression();
  }
  return result;
}

StmtPtr Parser::parseListMutation(bool remove) {
  if (remove) {
    consumeWord("item", "expected 'item' after 'remove'");
    consumeWord("of", "expected 'of' after 'remove item'");
    auto result = std::make_unique<ListMutationStmt>();
    result->operation = ListMutationStmt::Operation::RemoveLast;
    result->name = consume(TokenType::Identifier, "expected list name").text;
    return result;
  }
  auto result = std::make_unique<ListMutationStmt>();
  result->operation = ListMutationStmt::Operation::Append;
  result->value = parseExpression();
  consumeWord("on", "expected 'on' after value to append");
  result->name = consume(TokenType::Identifier, "expected list name after 'on'").text;
  return result;
}

StmtPtr Parser::parseExpressionStatement() {
  auto result = std::make_unique<ExpressionStmt>();
  result->expression = parseExpression();
  return result;
}

StmtPtr Parser::parseStruct() {
  Token name = consume(TokenType::Identifier, "expected struct name");
  requireName(name, "struct");
  auto structure = std::make_unique<StructStmt>();
  structure->name = name.text;
  consume(TokenType::LeftBrace, "expected '{' after struct name");
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    Token type = consume(TokenType::Identifier, "expected struct field type");
    if (!isTypeWord(type.text))
      errorAt(type, "unsupported struct field type '" + type.text + "'");
    Token field = consume(TokenType::Identifier, "expected struct field name");
    requireName(field, "field");
    structure->fields.push_back({parseTypeWord(type.text), field.text, true});
  }
  consume(TokenType::RightBrace, "expected '}' after struct fields");
  return structure;
}

StmtPtr Parser::parseEnum() {
  Token name = consume(TokenType::Identifier, "expected enum name");
  requireName(name, "enum");
  auto enumeration = std::make_unique<EnumStmt>();
  enumeration->name = name.text;
  consume(TokenType::LeftBrace, "expected '{' after enum name");
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    if (match(TokenType::Comma))
      continue;
    const Token &member = consume(TokenType::Identifier, "expected enum member name");
    requireName(member, "enum member");
    enumeration->members.push_back(member.text);
  }
  consume(TokenType::RightBrace, "expected '}' after enum members");
  if (enumeration->members.empty())
    errorAt(name, "enum must contain at least one member");
  return enumeration;
}

StmtPtr Parser::parseClassMember(ClassStmt &klass, bool isPublic, bool abstractMethod) {
  if (matchWord("initialize")) {
    Token type = consume(TokenType::Identifier, "expected field type");
    if (!isTypeWord(type.text))
      errorAt(type, "unsupported field type '" + type.text + "'");
    Token field = consume(TokenType::Identifier, "expected field name");
    requireName(field, "field");
    klass.fields.push_back({parseTypeWord(type.text), field.text, isPublic});
    return nullptr;
  }

  if (matchWord("define")) {
    if (matchWord("constructor")) {
      if (klass.hasConstructor)
        errorAt(previous(), "class already has a constructor");
      klass.hasConstructor = true;
      klass.constructorPublic = isPublic;
      consume(TokenType::LeftBrace, "expected '{' before constructor parameters");
      klass.constructorParameters = parseParameters(TokenType::RightBrace);
      consume(TokenType::RightBrace, "expected '}' after constructor parameters");
      klass.constructorBody = parseBlock();
      return nullptr;
    }
    consumeWord("function", "expected 'function' or 'constructor' after 'define'");
    Token methodName = consume(TokenType::Identifier, "expected method name");
    requireName(methodName, "method");
    ClassMethod method;
    method.name = methodName.text;
    method.isPublic = isPublic;
    method.isAbstract = abstractMethod;
    consume(TokenType::LeftParen, "expected '(' after method name");
    method.parameters = parseParameters(TokenType::RightParen);
    consume(TokenType::RightParen, "expected ')' after method parameters");
    consumeWord("returning", "expected 'returning' after method signature");
    Token returnType = consume(TokenType::Identifier, "expected method return type");
    if (returnType.text == "nothing") {
      method.returnType = ValueType::Void;
    } else if (isTypeWord(returnType.text)) {
      errorAt(returnType, "class methods must currently return 'nothing'");
    } else {
      errorAt(returnType, "unsupported method return type '" + returnType.text + "'");
    }
    if (!method.isAbstract) {
      method.body = parseBlock();
    }
    klass.methods.push_back(std::move(method));
    return nullptr;
  }

  errorAtCurrent("expected field or method declaration in class");
}

StmtPtr Parser::parseMethodCall() {
  Token method = consume(TokenType::Identifier, "expected method name after 'call'");
  consume(TokenType::LeftParen, "expected '(' after method name");
  std::vector<ExprPtr> arguments;
  if (!check(TokenType::RightParen)) {
    do {
      arguments.push_back(parseExpression());
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightParen, "expected ')' after method arguments");
  consumeWord("from", "expected 'from' after method call");
  Token object = consume(TokenType::Identifier, "expected object name after 'from'");
  auto node = std::make_unique<MethodCallStmt>();
  node->method = method.text;
  node->objectName = object.text;
  node->arguments = std::move(arguments);
  return node;
}

StmtPtr Parser::parseSuperConstructorCall() {
  auto node = std::make_unique<SuperConstructorCallStmt>();
  consume(TokenType::LeftParen, "expected '(' after 'super'");
  node->arguments = parseArguments();
  consume(TokenType::RightParen, "expected ')' after super arguments");
  return node;
}

StmtPtr Parser::parseSet() {
  Token name = consume(TokenType::Identifier, "expected variable name after 'set'");
  if (matchWord("of")) {
    Token object = consume(TokenType::Identifier, "expected object name after 'of'");
    consumeWord("to", "expected keyword 'to'");
    auto node = std::make_unique<MemberAssignmentStmt>();
    node->field = name.text;
    node->objectName = object.text;
    node->expr = parseExpression();
    return node;
  }
  consumeWord("to", "expected keyword 'to'");
  ExprPtr expr = parseExpression();
  return std::make_unique<AssignmentStmt>(name.text, std::move(expr));
}

StmtPtr Parser::parseOutput() {
  ExprPtr expr = parseExpression();
  return std::make_unique<OutputStmt>(std::move(expr));
}

StmtPtr Parser::parseInput() {
  ExprPtr prompt = parseExpression();
  consumeWord("into", "expected 'into' after input prompt");
  Token name = consume(TokenType::Identifier, "expected target variable after 'into'");
  auto node = std::make_unique<InputStmt>();
  node->prompt = std::move(prompt);
  node->name = name.text;
  return node;
}

StmtPtr Parser::parseCompound(const std::string &op) {
  if (op == "divide") {
    Token name =
        consume(TokenType::Identifier, "expected target variable after arithmetic command");
    consumeWord("by", "expected keyword 'by'");
    ExprPtr amount = parseExpression();
    return std::make_unique<AddToStmt>(op, std::move(amount), name.text);
  }

  if (op == "mod" && check(TokenType::Identifier) && current + 1 < tokens.size() &&
      tokens[current + 1].text == "by") {
    Token name = tokens[current++];
    consumeWord("by", "expected keyword 'by'");
    ExprPtr amount = parseExpression();
    return std::make_unique<AddToStmt>(op, std::move(amount), name.text);
  }

  ExprPtr amount = parseExpression();
  if (op == "mod")
    consumeWord("by", "expected keyword 'by'");
  else if (op == "add" || op == "multiply")
    consumeWord("to", "expected keyword 'to'");
  else
    consumeWord("from", "expected keyword 'from'");
  Token name = consume(TokenType::Identifier, "expected target variable");
  return std::make_unique<AddToStmt>(op, std::move(amount), name.text);
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  consume(TokenType::LeftBrace, "expected '{'");
  auto block = std::make_unique<BlockStmt>();
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    block->statements.push_back(parseStatement());
  }
  consume(TokenType::RightBrace, "expected '}'");
  return block;
}

StmtPtr Parser::parseIf() {
  // A condition may start with '(' without being wrapped in one pair of
  // parentheses, e.g. '(x > 1) and (y > 1)', so it is parsed as a whole
  // expression. parsePrimary handles the parentheses.
  ExprPtr condition = parseNaturalCondition();

  auto thenBlock = parseBlock();
  std::unique_ptr<BlockStmt> elseBlock;
  if (matchWord("else") || matchWord("otherwise")) {
    if (checkWord("if")) {
      errorAtCurrent("'" + previous().text +
                     " if' is not supported; put a new 'if' "
                     "inside the " +
                     previous().text + " { } block instead");
    }
    elseBlock = parseBlock();
  }

  auto node = std::make_unique<IfStmt>();
  node->condition = std::move(condition);
  node->thenBlock = std::move(thenBlock);
  node->elseBlock = std::move(elseBlock);
  return node;
}

StmtPtr Parser::parseCheck() {
  auto result = std::make_unique<CheckStmt>();
  result->value = parseExpression();
  consume(TokenType::LeftBrace, "expected '{' after check expression");
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    if (matchWord("when")) {
      CheckCase item;
      item.value = parseExpression();
      item.body = parseBlock();
      result->cases.push_back(std::move(item));
    } else if (matchWord("otherwise")) {
      if (result->otherwise)
        errorAt(previous(), "check can have only one otherwise case");
      result->otherwise = parseBlock();
    } else {
      errorAtCurrent("expected 'when' or 'otherwise' in check");
    }
  }
  consume(TokenType::RightBrace, "expected '}' after check cases");
  return result;
}

StmtPtr Parser::parseForEach() {
  Token name = consume(TokenType::Identifier, "expected iteration variable after 'each'");
  requireName(name, "variable");
  consumeWord("in", "expected 'in' after iteration variable");
  auto result = std::make_unique<ForEachStmt>();
  result->name = name.text;
  result->collection = parseExpression();
  result->body = parseBlock();
  return result;
}

namespace {
bool isLogicalExpression(const Expr *expression) {
  auto *binary = dynamic_cast<const BinaryExpr *>(expression);
  return binary && (binary->op == "and" || binary->op == "or");
}
} // namespace

ExprPtr Parser::parseNaturalCondition() {
  ExprPtr condition = parseNaturalComparison();
  if (checkWord("and") || checkWord("or")) {
    errorAtCurrent("an English comparison must be the whole condition and cannot be "
                   "combined with '" +
                   peek().text +
                   "'; use symbols instead, e.g. "
                   "if (x > 1 " +
                   peek().text + " y > 1)");
  }
  return condition;
}

ExprPtr Parser::parseNaturalComparison() {
  // The right-hand side of an English comparison is parsed with parseEquality so it
  // stops before 'and'/'or'; parseNaturalCondition then reports the combination.
  ExprPtr left = parseExpression();
  if (checkWord("is") && isLogicalExpression(left.get())) {
    errorAtCurrent("an English comparison must be the whole condition and cannot be "
                   "combined with 'and'/'or'; use symbols instead, e.g. "
                   "if (x > 1 and y > 1)");
  }
  if (matchWord("is")) {
    if (matchWord("not")) {
      consumeWord("equal", "expected 'equal' after 'is not'");
      consumeWord("to", "expected 'to' after 'is not equal'");
      return std::make_unique<BinaryExpr>("!=", std::move(left), parseEquality());
    }
    if (matchWord("greater")) {
      consumeWord("than", "expected 'than' after 'greater'");
      if (matchWord("or")) {
        consumeWord("equal", "expected 'equal' after 'or'");
        consumeWord("to", "expected 'to' after 'equal'");
        return std::make_unique<BinaryExpr>(">=", std::move(left), parseEquality());
      }
      return std::make_unique<BinaryExpr>(">", std::move(left), parseEquality());
    }
    if (matchWord("less")) {
      consumeWord("than", "expected 'than' after 'less'");
      if (matchWord("or")) {
        consumeWord("equal", "expected 'equal' after 'or'");
        consumeWord("to", "expected 'to' after 'equal'");
        return std::make_unique<BinaryExpr>("<=", std::move(left), parseEquality());
      }
      return std::make_unique<BinaryExpr>("<", std::move(left), parseEquality());
    }
    if (matchWord("equal")) {
      consumeWord("to", "expected 'to' after 'equal'");
      return std::make_unique<BinaryExpr>("==", std::move(left), parseEquality());
    }
    errorAtCurrent("expected a supported comparison after 'is'");
  }
  return left;
}

StmtPtr Parser::parseWhile() {
  ExprPtr condition = parseNaturalCondition();
  matchWord("do");
  auto body = parseBlock();
  auto node = std::make_unique<WhileStmt>();
  node->condition = std::move(condition);
  node->body = std::move(body);
  return node;
}

StmtPtr Parser::parseRepeat() {
  ExprPtr count = parseExpression();
  consumeWord("times", "expected keyword 'times' after repeat count");
  auto body = parseBlock();
  auto node = std::make_unique<RepeatStmt>();
  node->count = std::move(count);
  node->body = std::move(body);
  return node;
}

StmtPtr Parser::parseFor() {
  consume(TokenType::LeftParen, "expected '(' after 'for'");

  StmtPtr init;
  if (matchWord("int"))
    init = parseDeclaration(ValueType::Int);
  else if (matchWord("float"))
    init = parseDeclaration(ValueType::Float);
  else if (matchWord("string"))
    init = parseDeclaration(ValueType::String);
  else {
    Token name = consume(TokenType::Identifier, "expected for-loop initialization");
    consume(TokenType::Assign, "expected '=' in for-loop initialization");
    ExprPtr expr = parseExpression();
    init = std::make_unique<AssignmentStmt>(name.text, std::move(expr));
  }

  consume(TokenType::Semicolon, "expected ';' after for-loop initialization");
  ExprPtr condition = parseExpression();
  consume(TokenType::Semicolon, "expected ';' after for-loop condition");

  Token incName = consume(TokenType::Identifier, "expected variable in for-loop increment");
  consume(TokenType::Increment, "expected '++' in for-loop increment");
  StmtPtr increment = std::make_unique<AssignmentStmt>(
      incName.text, std::make_unique<BinaryExpr>("+", std::make_unique<VariableExpr>(incName.text),
                                                 std::make_unique<LiteralExpr>(Value::makeInt(1))));

  consume(TokenType::RightParen, "expected ')' after for-loop");
  auto body = parseBlock();

  auto node = std::make_unique<ForStmt>();
  node->init = std::move(init);
  node->condition = std::move(condition);
  node->increment = std::move(increment);
  node->body = std::move(body);
  return node;
}

ExprPtr Parser::parseExpression() {
  return parseLogicalOr();
}

ExprPtr Parser::parseEquality() {
  ExprPtr expr = parseComparison();
  while (check(TokenType::Equal) || check(TokenType::NotEqual)) {
    std::string op = peek().text;
    ++current;
    expr = std::make_unique<BinaryExpr>(op, std::move(expr), parseComparison());
  }
  return expr;
}

ExprPtr Parser::parseLogicalOr() {
  ExprPtr expr = parseLogicalAnd();
  while (matchWord("or")) {
    expr = std::make_unique<BinaryExpr>("or", std::move(expr), parseLogicalAnd());
  }
  return expr;
}

ExprPtr Parser::parseLogicalAnd() {
  ExprPtr expr = parseEquality();
  while (matchWord("and")) {
    expr = std::make_unique<BinaryExpr>("and", std::move(expr), parseEquality());
  }
  return expr;
}

ExprPtr Parser::parseComparison() {
  ExprPtr expr = parseTerm();
  while (check(TokenType::Less) || check(TokenType::LessEqual) || check(TokenType::Greater) ||
         check(TokenType::GreaterEqual)) {
    std::string op = peek().text;
    ++current;
    expr = std::make_unique<BinaryExpr>(op, std::move(expr), parseTerm());
  }
  return expr;
}

ExprPtr Parser::parseTerm() {
  ExprPtr expr = parseFactor();
  while (check(TokenType::Plus) || check(TokenType::Minus)) {
    std::string op = peek().text;
    ++current;
    expr = std::make_unique<BinaryExpr>(op, std::move(expr), parseFactor());
  }
  return expr;
}

ExprPtr Parser::parseFactor() {
  ExprPtr expr = parseUnary();
  while (check(TokenType::Star) || check(TokenType::Slash) ||
         (checkWord("mod") && peek().pos.line == previous().pos.line)) {
    std::string op = peek().text;
    if (checkWord("mod")) {
      op = "mod";
      ++current;
    } else {
      ++current;
    }
    expr = std::make_unique<BinaryExpr>(op, std::move(expr), parseUnary());
  }
  return expr;
}

ExprPtr Parser::parseUnary() {
  if (matchWord("not"))
    return std::make_unique<UnaryExpr>("not", parseUnary());
  if (match(TokenType::Minus))
    return std::make_unique<UnaryExpr>("-", parseUnary());
  if (match(TokenType::Plus))
    return std::make_unique<UnaryExpr>("+", parseUnary());
  return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
  if (match(TokenType::Number)) {
    const std::string &text = previous().text;
    const bool isFloat = text.find('.') != std::string::npos;
    try {
      if (isFloat)
        return std::make_unique<LiteralExpr>(Value::makeFloat(std::stod(text)));
      return std::make_unique<LiteralExpr>(Value::makeInt(std::stoll(text)));
    } catch (const std::out_of_range &) {
      std::string literalType = "integer";
      if (isFloat) {
        literalType = "float";
      }
      errorAt(previous(), literalType + " literal out of range");
    }
  }

  if (match(TokenType::String)) {
    return std::make_unique<LiteralExpr>(Value::makeString(previous().text));
  }

  if (match(TokenType::Identifier)) {
    const std::string name = previous().text;
    if (name == "true")
      return std::make_unique<LiteralExpr>(Value::makeBool(true));
    if (name == "false")
      return std::make_unique<LiteralExpr>(Value::makeBool(false));
    if ((name == "item" || name == "length" || name == "top") && matchWord("of")) {
      auto result = std::make_unique<ListExpr>();
      if (name == "item") {
        result->operation = ListExpr::Operation::Index;
      } else if (name == "length") {
        result->operation = ListExpr::Operation::Length;
      } else {
        result->operation = ListExpr::Operation::Top;
      }
      result->name = consume(TokenType::Identifier, "expected list name after 'of'").text;
      if (name == "item") {
        consumeWord("at", "expected 'at' after list name");
        // Arithmetic only, so 'item of l at 0 > 5' compares the item, not the index.
        result->index = parseTerm();
      }
      return result;
    }
    if (match(TokenType::LeftParen)) {
      auto result = std::make_unique<CallExpr>();
      result->name = name;
      result->arguments = parseArguments();
      consume(TokenType::RightParen, "expected ')' after function arguments");
      return result;
    }
    if (matchWord("of")) {
      Token object = consume(TokenType::Identifier, "expected object name after 'of'");
      return std::make_unique<MemberExpr>(name, std::make_unique<VariableExpr>(object.text));
    }

    return std::make_unique<VariableExpr>(name);
  }

  if (match(TokenType::LeftParen)) {
    ExprPtr expr = parseExpression();
    consume(TokenType::RightParen, "expected ')' after expression");
    return expr;
  }

  errorAtCurrent("expected expression");
}

std::vector<ExprPtr> Parser::parseArguments() {
  std::vector<ExprPtr> arguments;
  if (!check(TokenType::RightParen)) {
    do {
      arguments.push_back(parseExpression());
    } while (match(TokenType::Comma));
  }
  return arguments;
}

Program Parser::parseProgram() {
  Program program;
  while (!isAtEnd()) {
    program.push_back(parseStatement());
  }
  return program;
}
