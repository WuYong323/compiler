#include "dump.h"

namespace cool {
namespace {

const char* binOpName(BinOp op) {
  switch (op) {
    case BinOp::Plus: return "+";
    case BinOp::Minus: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
    case BinOp::Lt: return "<";
    case BinOp::Le: return "<=";
    case BinOp::Eq: return "=";
  }
  return "?";
}

// 用 Visitor 打印 AST 结构树
class Dumper : public ExprVisitor {
  std::ostream& os_;
  int indent_ = 0;
  void pad() const { for (int i = 0; i < indent_; ++i) os_ << "  "; }
  void child(const Expr& e) { ++indent_; e.accept(*this); --indent_; }

public:
  explicit Dumper(std::ostream& os, int indent = 0) : os_(os), indent_(indent) {}

  void visit(const IntConst& e) override    { pad(); os_ << "int " << e.value << "\n"; }
  void visit(const StringConst& e) override { pad(); os_ << "string \"" << e.value << "\"\n"; }
  void visit(const BoolConst& e) override   { pad(); os_ << (e.value ? "true" : "false") << "\n"; }
  void visit(const Object& e) override      { pad(); os_ << "object " << e.name << "\n"; }
  void visit(const Assign& e) override {
    pad(); os_ << "assign " << e.name << "\n";
    child(*e.rhs);
  }
  void visit(const Dispatch& e) override {
    pad(); os_ << "dispatch ." << e.method << "\n";
    child(*e.receiver);
    for (auto& a : e.args) child(*a);
  }
  void visit(const StaticDispatch& e) override {
    pad(); os_ << "static-dispatch @" << e.type << "." << e.method << "\n";
    child(*e.receiver);
    for (auto& a : e.args) child(*a);
  }
  void visit(const If& e) override {
    pad(); os_ << "if\n";
    child(*e.cond); child(*e.then_); child(*e.else_);
  }
  void visit(const While& e) override {
    pad(); os_ << "while\n";
    child(*e.cond); child(*e.body);
  }
  void visit(const Block& e) override {
    pad(); os_ << "block\n";
    for (auto& x : e.exprs) child(*x);
  }
  void visit(const Let& e) override {
    pad(); os_ << "let\n";
    for (auto& b : e.bindings) {
      pad(); os_ << "  bind " << b.name << " : " << b.type << "\n";
      if (b.init) child(*b.init);
    }
    child(*e.body);
  }
  void visit(const Case& e) override {
    pad(); os_ << "case\n";
    child(*e.scrutinee);
    for (auto& b : e.branches) {
      pad(); os_ << "  branch " << b.name << " : " << b.type << "\n";
      child(*b.body);
    }
  }
  void visit(const New& e) override { pad(); os_ << "new " << e.type << "\n"; }
  void visit(const IsVoid& e) override {
    pad(); os_ << "isvoid\n";
    child(*e.expr);
  }
  void visit(const Binary& e) override {
    pad(); os_ << "binary " << binOpName(e.op) << "\n";
    child(*e.lhs); child(*e.rhs);
  }
  void visit(const Neg& e) override {
    pad(); os_ << "neg\n";
    child(*e.expr);
  }
  void visit(const Not& e) override {
    pad(); os_ << "not\n";
    child(*e.expr);
  }
  void visit(const NoExpr&) override { pad(); os_ << "no-expr\n"; }
};

void dumpFeature(const Feature& f, std::ostream& os, int indent) {
  auto pad = [&]() { for (int i = 0; i < indent; ++i) os << "  "; };
  pad();
  if (auto* m = dynamic_cast<const Method*>(&f)) {
    os << "method " << m->name << "(";
    for (size_t i = 0; i < m->formals.size(); ++i) {
      if (i) os << ", ";
      os << m->formals[i].name << " : " << m->formals[i].type;
    }
    os << ") : " << m->type << "\n";
    Dumper d(os, indent + 1);
    m->body->accept(d);
  } else if (auto* a = dynamic_cast<const Attribute*>(&f)) {
    os << "attribute " << a->name << " : " << a->type;
    if (a->init) {
      os << " <-\n";
      Dumper d(os, indent + 1);
      a->init->accept(d);
    } else {
      os << "\n";
    }
  }
}

} // namespace

void dumpProgram(const Program& p, std::ostream& os) {
  os << "program\n";
  for (auto& c : p.classes) {
    os << "  class " << c->name;
    if (c->parent != "Object") os << " inherits " << c->parent;
    os << "\n";
    for (auto& f : c->features) dumpFeature(*f, os, 2);
  }
}

} // namespace cool
