#pragma once

#include <string>

enum class TokenType {
  Identifier,
  Number,
  String,
  Plus,
  Minus,
  Star,
  Slash,
  Assign,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Increment,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Semicolon,
  Comma,
  ShiftLeft,
  End
};

struct SourcePos {
  int line = 1;
  int column = 1;
};

struct Token {
  TokenType type;
  std::string text;
  SourcePos pos;
};

std::string tokenName(TokenType type);
