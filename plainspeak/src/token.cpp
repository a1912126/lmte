#include "token.hpp"

std::string tokenName(TokenType t) {
  switch (t) {
  case TokenType::Identifier:
    return "identifier";
  case TokenType::Number:
    return "number";
  case TokenType::String:
    return "string";
  case TokenType::Plus:
    return "+";
  case TokenType::Minus:
    return "-";
  case TokenType::Star:
    return "*";
  case TokenType::Slash:
    return "/";
  case TokenType::Assign:
    return "=";
  case TokenType::Equal:
    return "==";
  case TokenType::NotEqual:
    return "!=";
  case TokenType::Less:
    return "<";
  case TokenType::LessEqual:
    return "<=";
  case TokenType::Greater:
    return ">";
  case TokenType::GreaterEqual:
    return ">=";
  case TokenType::Increment:
    return "++";
  case TokenType::LeftParen:
    return "(";
  case TokenType::RightParen:
    return ")";
  case TokenType::LeftBrace:
    return "{";
  case TokenType::RightBrace:
    return "}";
  case TokenType::Semicolon:
    return ";";
  case TokenType::Comma:
    return ",";
  case TokenType::End:
    return "end of file";
  }
  return "token";
}
