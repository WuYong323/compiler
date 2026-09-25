#pragma once
#include <memory>
#include <string>
#include <vector>

namespace cool {

// ================= 前向声明（供 Visitor 使用） =================
struct IntConst;    struct StringConst; struct BoolConst;  struct Object;
struct Assign;      struct Dispatch;    struct StaticDispatch;
struct If;          struct While;       struct Block;
struct Let;         struct Case;        struct New;
struct IsVoid;      struct Binary;      struct Neg;
struct Not;         struct NoExpr;

// ================= 表达式 Visitor 接口 =================
struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual void visit(const IntConst&) = 0;
  virtual void visit(const StringConst&) = 0;
  virtual void visit(const BoolConst&) = 0;
  virtual void visit(const Object&) = 0;
  virtual void visit(const Assign&) = 0;
  virtual void visit(const Dispatch&) = 0;
  virtual void visit(const StaticDispatch&) = 0;
  virtual void visit(const If&) = 0;
  virtual void visit(const While&) = 0;
  virtual void visit(const Block&) = 0;
  virtual void visit(const Let&) = 0;
  virtual void visit(const Case&) = 0;
  virtual void visit(const New&) = 0;
  virtual void visit(const IsVoid&) = 0;
  virtual void visit(const Binary&) = 0;
  virtual void visit(const Neg&) = 0;
  virtual void visit(const Not&) = 0;
  virtual void visit(const NoExpr&) = 0;
};

// ================= 表达式节点 =================
struct Expr {
  int line = 0;
  std::string type;   // 类型检查后填入的静态类型（codegen 使用）
  virtual ~Expr() = default;
  virtual void accept(ExprVisitor& v) const = 0;
};

struct IntConst : Expr {
  int value = 0; std::string lexeme;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct StringConst : Expr {
  std::string value;   // 词素本身（\t \n 保持两字符，不翻译）
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct BoolConst : Expr {
  bool value = false;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Object : Expr {   // 标识符引用（含 self）
  std::string name;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Assign : Expr {
  std::string name; std::unique_ptr<Expr> rhs;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Dispatch : Expr {   // e.f(args)；self-dispatch 时 receiver=Object("self")
  std::unique_ptr<Expr> receiver; std::string method;
  std::vector<std::unique_ptr<Expr>> args;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct StaticDispatch : Expr {   // e@T.f(args)
  std::unique_ptr<Expr> receiver; std::string type; std::string method;
  std::vector<std::unique_ptr<Expr>> args;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct If : Expr {
  std::unique_ptr<Expr> cond, then_, else_;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct While : Expr {
  std::unique_ptr<Expr> cond, body;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Block : Expr {
  std::vector<std::unique_ptr<Expr>> exprs;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Binding {
  std::string name; std::string type; std::unique_ptr<Expr> init; // init 可为空
};
struct Let : Expr {
  std::vector<Binding> bindings; std::unique_ptr<Expr> body;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct CaseBranch {
  std::string name; std::string type; std::unique_ptr<Expr> body;
};
struct Case : Expr {
  std::unique_ptr<Expr> scrutinee; std::vector<CaseBranch> branches;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct New : Expr {
  std::string type;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct IsVoid : Expr {
  std::unique_ptr<Expr> expr;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
enum class BinOp { Plus, Minus, Mul, Div, Lt, Le, Eq };
struct Binary : Expr {
  BinOp op; std::unique_ptr<Expr> lhs, rhs;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Neg : Expr {
  std::unique_ptr<Expr> expr;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct Not : Expr {
  std::unique_ptr<Expr> expr;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
struct NoExpr : Expr {
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};

// ================= 特性（attribute / method） =================
struct Formal { std::string name; std::string type; };

struct Feature {
  int line = 0;
  std::string name;
  std::string type;   // 属性类型 / 方法返回类型
  virtual ~Feature() = default;
};
struct Attribute : Feature { std::unique_ptr<Expr> init; };  // init 可为空
struct Method : Feature {
  std::vector<Formal> formals; std::unique_ptr<Expr> body;
};

// ================= 类与程序 =================
struct ClassDef {
  std::string name; std::string parent;
  std::vector<std::unique_ptr<Feature>> features;
  int line = 0;
};
struct Program {
  std::vector<std::unique_ptr<ClassDef>> classes;
};

} // namespace cool
