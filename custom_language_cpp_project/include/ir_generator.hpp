#pragma once

#include "ast.hpp"
#include <string>
#include <vector>

class IRGenerator {
  int temp = 0;
  int label = 0;
  std::vector<std::string> lines;

  std::string nextTemp();
  std::string nextLabel(const std::string &prefix);
  std::string expr(const Expr *expression);
  void stmt(const Stmt *statement);
  void block(const BlockStmt *blockStmt);

public:
  std::vector<std::string> generate(const Program &program);
};
