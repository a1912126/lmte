#pragma once

#include "token.hpp"
#include <string>
#include <vector>

class Lexer {
    const std::string& source;
    std::size_t index = 0;
    int line = 1;
    int column = 1;

    char peek(std::size_t offset = 0) const;
    char advance();
    SourcePos pos() const;
    void addToken(std::vector<Token>& out, TokenType type, const std::string& text, SourcePos p);

public:
    explicit Lexer(const std::string& src);
    std::vector<Token> tokenize();
};
