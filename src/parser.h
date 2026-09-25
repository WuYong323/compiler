#pragma once
#include <memory>
#include <string>
#include <vector>
#include "lexer.h"
#include "ast.h"

namespace cool {

// 递归下降 + 优先级爬升的语法分析器（2 token 前瞻）
class Parser {
public:
  explicit Parser(std::string src) : lexer_(std::move(src)) {
    cur_ = lexer_.next();
    nxt_ = lexer_.next();
  }
  Program parseProgram();

private:
  Lexer lexer_;
  Token cur_;   // 当前 token
  Token nxt_;   // 下一个 token（前瞻 2）

  void advance() { cur_ = nxt_; nxt_ = lexer_.next(); }
  bool at(Tok k) const { return cur_.kind == k; }
  [[noreturn]] void error(const std::string& msg) const;
  Token expect(Tok k, const std::string& what);

  // 类 / 特性
  std::unique_ptr<ClassDef> parseClass();
  std::unique_ptr<Feature> parseFeature();

  // 表达式（按优先级分层，由松到紧）
  std::unique_ptr<Expr> parseExpr();      // <-（赋值，右结合，最松）
  std::unique_ptr<Expr> parseNot();       // not
  std::unique_ptr<Expr> parseCompare();   // <= < =（非结合）
  std::unique_ptr<Expr> parseAddSub();    // + -
  std::unique_ptr<Expr> parseMulDiv();    // * /
  std::unique_ptr<Expr> parseIsvoid();    // isvoid
  std::unique_ptr<Expr> parseNeg();       // ~
  std::unique_ptr<Expr> parseDispatch();  // . @ 后缀（最紧）
  std::unique_ptr<Expr> parsePrimary();   // 原子

  // 辅助
  std::unique_ptr<Expr> parseIf();
  std::unique_ptr<Expr> parseWhile();
  std::unique_ptr<Expr> parseBlock();
  std::unique_ptr<Expr> parseLet();
  std::unique_ptr<Expr> parseCase();
  std::vector<std::unique_ptr<Expr>> parseDispatchArgs(); // 已见 '('，解析到 ')'
};

} // namespace cool
