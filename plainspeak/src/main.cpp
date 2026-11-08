#include "common.hpp"
#include "interpreter.hpp"
#include "ir_generator.hpp"
#include "lexer.hpp"
#include "llvm_backend.hpp"
#include "optimizer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw CompileError("cannot open input file '" + path + "'");
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

static void printUsage() {
  std::cout << "Usage: customlang <source-file.psk> [--tokens] [--ir] [--llvm-ir]\n";
}

static bool hasPskExtension(const std::string &path) {
  constexpr char extension[] = ".psk";
  return path.size() >= sizeof(extension) - 1 &&
         path.compare(path.size() - (sizeof(extension) - 1), sizeof(extension) - 1, extension) == 0;
}

static void printTokens(const std::vector<Token> &tokens) {
  std::cout << "=== TOKENS ===\n";
  for (const auto &token : tokens) {
    std::cout << token.pos.line << ':' << token.pos.column << "  " << tokenName(token.type);
    if (!token.text.empty())
      std::cout << "  '" << token.text << "'";
    std::cout << '\n';
  }
  std::cout << '\n';
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string sourcePath;
  bool showTokens = false;
  bool showIR = false;
  bool showLLVMIR = false;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--tokens")
      showTokens = true;
    else if (argument == "--ir")
      showIR = true;
    else if (argument == "--llvm-ir")
      showLLVMIR = true;
    else if (sourcePath.empty())
      sourcePath = argument;
    else {
      printUsage();
      return 1;
    }
  }

  // True once the program has passed every check and the interpreter is running.
  bool running = false;
  try {
    if (sourcePath.empty()) {
      printUsage();
      return 1;
    }
    if (!hasPskExtension(sourcePath)) {
      throw CompileError("source files must use the '.psk' extension");
    }

    const std::string source = readFile(sourcePath);

    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    if (showTokens)
      printTokens(tokens);

    Parser parser(std::move(tokens));
    Program program = parser.parseProgram();

    SemanticAnalyzer semantic;
    semantic.analyze(program);

    optimizeProgram(program);

    IRGenerator irGenerator;
    const std::vector<std::string> irLines = irGenerator.generate(program);
    if (showIR) {
      std::cout << "=== IR ===\n";
      for (std::size_t i = 0; i < irLines.size(); ++i) {
        std::cout << std::setw(4) << i << ": " << irLines[i] << '\n';
      }
      std::cout << '\n';
    }

    if (showLLVMIR) {
      std::cout << "=== OPTIMIZED LLVM IR (O2) ===\n";
      std::cout << generateOptimizedLLVMIR(program) << '\n';
    }

    Interpreter interpreter;
    running = true;
    interpreter.run(program);
  } catch (const CompileError &error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    if (!running) {
      // Any other exception before the program runs is a bug in the compiler itself,
      // not a run-time error in the user's program.
      std::cerr << "ERROR: internal compiler error: " << error.what() << '\n';
      return 1;
    }
    std::cerr << "RUNTIME ERROR: " << error.what() << '\n';
    return 2;
  }

  return 0;
}
