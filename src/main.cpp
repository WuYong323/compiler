#include <fstream>
#include <iostream>
#include <sstream>
#include "lexer.h"
using namespace cool;

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: coolc <file.cl>\n"; return 2; }
  std::ifstream in(argv[1]);
  if (!in) { std::cerr << "cannot open: " << argv[1] << "\n"; return 2; }
  std::stringstream ss; ss << in.rdbuf();
  Lexer lex(ss.str());

  bool hadError = false;
  for (;;) {
    Token t = lex.next();
    if (t.kind == Tok::END) break;
    if (t.kind == Tok::ERROR) {
      std::cerr << "ERROR: line " << t.line << ": " << t.lexeme << "\n";
      hadError = true;
      break;               // 本阶段遇错即停；错误恢复在 doc 03/05 完善
    }
    std::cout << "line " << t.line << ": " << tokName(t.kind)
              << "  '" << t.lexeme << "'\n";
  }
  return hadError ? 1 : 0;
}