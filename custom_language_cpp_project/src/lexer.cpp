#include "lexer.hpp"
#include "common.hpp"
#include <cctype>

Lexer::Lexer(const std::string &src) : source(src) {
}

char Lexer::peek(std::size_t offset) const {
  const std::size_t i = index + offset;
  if (i < source.size()) {
    return source[i];
  }
  return '\0';
}

char Lexer::advance() {
  const char c = peek();
  if (c == '\0')
    return c;
  ++index;
  if (c == '\n') {
    ++line;
    column = 1;
  } else {
    ++column;
  }
  return c;
}

SourcePos Lexer::pos() const {
  return {line, column};
}

void Lexer::addToken(std::vector<Token> &out, TokenType type, const std::string &text,
                     SourcePos p) {
  out.push_back({type, text, p});
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (true) {
    while (std::isspace(static_cast<unsigned char>(peek())))
      advance();

    const SourcePos p = pos();
    const char c = peek();

    if (c == '\0') {
      addToken(tokens, TokenType::End, "", p);
      return tokens;
    }

    if (c == '/' && peek(1) == '/') {
      while (peek() != '\0' && peek() != '\n')
        advance();
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      std::string text;
      while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
        text += advance();
      }
      addToken(tokens, TokenType::Identifier, text, p);
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
      std::string text;
      bool dot = false;
      while (std::isdigit(static_cast<unsigned char>(peek())) || (!dot && peek() == '.')) {
        if (peek() == '.')
          dot = true;
        text += advance();
      }
      addToken(tokens, TokenType::Number, text, p);
      continue;
    }

    if (c == '"') {
      advance();
      std::string text;

      while (peek() != '\0' && peek() != '"') {
        const char ch = advance();
        if (ch == '\\') {
          const char e = peek();
          if (e == 'n') {
            advance();
            text += '\n';
          } else if (e == 't') {
            advance();
            text += '\t';
          } else if (e == '"') {
            advance();
            text += '"';
          } else if (e == '\\') {
            advance();
            text += '\\';
          } else {
            if (e == '\0')
              throw CompileError("Line " + std::to_string(p.line) + ", column " +
                                 std::to_string(p.column) + ": unterminated string literal");
            text += advance();
          }
        } else {
          text += ch;
        }
      }

      if (peek() != '"') {
        throw CompileError("Line " + std::to_string(p.line) + ", column " +
                           std::to_string(p.column) + ": unterminated string literal");
      }
      advance();
      addToken(tokens, TokenType::String, text, p);
      continue;
    }

    if (c == '+' && peek(1) == '+') {
      advance();
      advance();
      addToken(tokens, TokenType::Increment, "++", p);
      continue;
    }
    if (c == '=' && peek(1) == '=') {
      advance();
      advance();
      addToken(tokens, TokenType::Equal, "==", p);
      continue;
    }
    if (c == '!' && peek(1) == '=') {
      advance();
      advance();
      addToken(tokens, TokenType::NotEqual, "!=", p);
      continue;
    }
    if (c == '<' && peek(1) == '=') {
      advance();
      advance();
      addToken(tokens, TokenType::LessEqual, "<=", p);
      continue;
    }
    if (c == '>' && peek(1) == '=') {
      advance();
      advance();
      addToken(tokens, TokenType::GreaterEqual, ">=", p);
      continue;
    }
    if (c == '<' && peek(1) == '<') {
      advance();
      advance();
      addToken(tokens, TokenType::ShiftLeft, "<<", p);
      continue;
    }

    TokenType type;
    switch (c) {
    case '+':
      type = TokenType::Plus;
      break;
    case '-':
      type = TokenType::Minus;
      break;
    case '*':
      type = TokenType::Star;
      break;
    case '/':
      type = TokenType::Slash;
      break;
    case '=':
      type = TokenType::Assign;
      break;
    case '<':
      type = TokenType::Less;
      break;
    case '>':
      type = TokenType::Greater;
      break;
    case '(':
      type = TokenType::LeftParen;
      break;
    case ')':
      type = TokenType::RightParen;
      break;
    case '{':
      type = TokenType::LeftBrace;
      break;
    case '}':
      type = TokenType::RightBrace;
      break;
    case ';':
      type = TokenType::Semicolon;
      break;
    case ',':
      type = TokenType::Comma;
      break;
    default:
      throw CompileError("Line " + std::to_string(p.line) + ", column " + std::to_string(p.column) +
                         ": unexpected character '" + std::string(1, c) + "'");
    }

    advance();
    addToken(tokens, type, std::string(1, c), p);
  }
}
