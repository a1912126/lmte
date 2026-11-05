#pragma once

#include "ast.hpp"
#include <string>

std::string generateOptimizedLLVMIR(const Program& program);
