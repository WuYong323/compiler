#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "lexer.h"
#include "parser.h"
#include "dump.h"
#include "typechecker.h"
#include "codegen.h"
using namespace cool;

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: coolc <file.cl> [--dump] [-o out.ll]\n"; return 2; }
  std::string inFile = argv[1];
  std::string outFile;
  bool dumpFlag = false;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--dump") dumpFlag = true;
    else if (a == "-o" && i + 1 < argc) outFile = argv[++i];
    else { std::cerr << "unknown arg: " << a << "\n"; return 2; }
  }
  std::ifstream in(inFile);
  if (!in) { std::cerr << "cannot open: " << inFile << "\n"; return 2; }
  std::stringstream ss; ss << in.rdbuf();

  try {
    Parser parser(ss.str());
    Program prog = parser.parseProgram();
    if (dumpFlag) { dumpProgram(prog, std::cout); return 0; }

    TypeChecker tc(prog);
    if (!tc.check()) { std::cerr << "type checking failed\n"; return 1; }

    CodeGen cg(prog);
    std::string ir = cg.generate();
    if (outFile.empty()) outFile = "out.ll";
    std::ofstream out(outFile);
    out << ir;
    std::cout << "wrote " << outFile << " (" << ir.size() << " bytes)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}
