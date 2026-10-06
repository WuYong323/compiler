#include "typechecker.h"
#include <iostream>

namespace cool {

void TypeChecker::error(int line, const std::string& msg) {
  ++errorCount_;
  std::cerr << "ERROR: line " << line << ": " << msg << "\n";
}

bool TypeChecker::isType(const std::string& t) const {
  return t == "SELF_TYPE" || t == "Object" || t == "IO" || t == "Int" ||
         t == "String" || t == "Bool" || classMap_.count(t);
}

// ---- 环境构建 ----

void TypeChecker::installBuiltins() {
  { // Object
    auto& m = methodEnv_["Object"];
    m["abort"] = {{}, "Object"};
    m["type_name"] = {{}, "String"};
    m["copy"] = {{}, "SELF_TYPE"};
    attrEnv_["Object"] = {};
  }
  { // IO
    auto& m = methodEnv_["IO"];
    m = methodEnv_["Object"];
    m["out_string"] = {{"String"}, "SELF_TYPE"};
    m["out_int"] = {{"Int"}, "SELF_TYPE"};
    m["in_string"] = {{}, "String"};
    m["in_int"] = {{}, "Int"};
    attrEnv_["IO"] = {};
  }
  { // Int
    methodEnv_["Int"] = methodEnv_["Object"];
    attrEnv_["Int"] = {};
  }
  { // String
    auto& m = methodEnv_["String"];
    m = methodEnv_["Object"];
    m["length"] = {{}, "Int"};
    m["concat"] = {{"String"}, "String"};
    m["substr"] = {{"Int", "Int"}, "String"};
    attrEnv_["String"] = {};
  }
  { // Bool
    methodEnv_["Bool"] = methodEnv_["Object"];
    attrEnv_["Bool"] = {};
  }
}

bool TypeChecker::hasCycle(const std::string& cls,
                           std::unordered_set<std::string>& visiting,
                           std::unordered_set<std::string>& done) {
  if (done.count(cls)) return false;
  if (visiting.count(cls)) {
    error(classMap_.at(cls)->line, "inheritance cycle involving class '" + cls + "'");
    return true;
  }
  visiting.insert(cls);
  const std::string& parent = parentMap_.at(cls);
  if (classMap_.count(parent))            // 只沿程序类递归（内建类无环）
    hasCycle(parent, visiting, done);
  visiting.erase(cls);
  done.insert(cls);
  return false;
}

void TypeChecker::buildEnvironments() {
  installBuiltins();
  // 注册类 + 基本检查
  for (auto& c : prog_.classes) {
    if (c->name == "Object" || c->name == "IO" || c->name == "Int" ||
        c->name == "String" || c->name == "Bool") {
      error(c->line, "class '" + c->name + "' cannot be redefined");
      continue;
    }
    if (classMap_.count(c->name)) {
      error(c->line, "class '" + c->name + "' already defined");
      continue;
    }
    classMap_[c->name] = c.get();
    parentMap_[c->name] = c->parent.empty() ? "Object" : c->parent;
  }
  // 父类检查
  for (auto& kv : parentMap_) {
    const std::string& name = kv.first;
    const std::string& parent = kv.second;
    if (parent == "Int" || parent == "String" || parent == "Bool") {
      error(classMap_.at(name)->line,
            "class '" + name + "' cannot inherit from basic class '" + parent + "'");
    } else if (parent != "Object" && parent != "IO" && !classMap_.count(parent)) {
      error(classMap_.at(name)->line,
            "class '" + name + "' inherits from undefined class '" + parent + "'");
    }
  }
  // 继承环检查
  std::unordered_set<std::string> visiting, done;
  for (auto& c : prog_.classes)
    if (!done.count(c->name)) hasCycle(c->name, visiting, done);
  // Main 类必须存在
  if (!classMap_.count("Main"))
    error(0, "no 'Main' class defined");
}

// 方法环境（含继承 + 重写检查）
const std::unordered_map<std::string, MethodSig>&
TypeChecker::methodsOf(const std::string& cls) {
  auto it = methodEnv_.find(cls);
  if (it != methodEnv_.end()) return it->second;
  static const std::unordered_map<std::string, MethodSig> EMPTY;
  if (!classMap_.count(cls)) return EMPTY;   // 未知类（错误已在别处报告）

  const std::string& parent = parentMap_.at(cls);
  const auto& penv = methodsOf(parent);
  auto& env = methodEnv_[cls];
  env = penv;
  std::unordered_set<std::string> own;
  const ClassDef* cd = classMap_.at(cls);
  for (auto& f : cd->features) {
    auto* m = dynamic_cast<const Method*>(f.get());
    if (!m) continue;
    if (own.count(m->name)) {
      error(m->line, "method '" + m->name + "' defined multiple times in class '" + cls + "'");
      continue;
    }
    own.insert(m->name);
    MethodSig sig;
    for (auto& fm : m->formals) sig.argTypes.push_back(fm.type);
    sig.retType = m->type;
    auto pit = penv.find(m->name);
    if (pit != penv.end() && !(pit->second == sig))
      error(m->line, "method '" + m->name + "' overrides inherited method with incompatible signature");
    env[m->name] = sig;
  }
  return env;
}

// 属性环境（含继承；重定义继承属性是错误）
const std::unordered_map<std::string, std::string>&
TypeChecker::attrsOf(const std::string& cls) {
  auto it = attrEnv_.find(cls);
  if (it != attrEnv_.end()) return it->second;
  static const std::unordered_map<std::string, std::string> EMPTY;
  if (!classMap_.count(cls)) return EMPTY;

  const std::string& parent = parentMap_.at(cls);
  const auto& penv = attrsOf(parent);
  auto& env = attrEnv_[cls];
  env = penv;
  std::unordered_set<std::string> own;
  const ClassDef* cd = classMap_.at(cls);
  for (auto& f : cd->features) {
    auto* a = dynamic_cast<const Attribute*>(f.get());
    if (!a) continue;
    if (own.count(a->name)) {
      error(a->line, "attribute '" + a->name + "' defined multiple times in class '" + cls + "'");
      continue;
    }
    own.insert(a->name);
    if (env.count(a->name))
      error(a->line, "attribute '" + a->name + "' redefines inherited attribute");
    else
      env[a->name] = a->type;
  }
  return env;
}

// ---- 子类型 / 上界 ----

bool TypeChecker::conforms(const std::string& a, const std::string& b) {
  if (a == b) return true;
  if (b == "SELF_TYPE") return false;   // 仅 SELF_TYPE <= SELF_TYPE（已处理）
  std::string cur = (a == "SELF_TYPE") ? currentClass_ : a;
  for (;;) {
    if (cur == b) return true;
    if (cur == "Object") return false;
    if (parentMap_.count(cur)) cur = parentMap_.at(cur);
    else if (cur == "IO" || cur == "Int" || cur == "String" || cur == "Bool") cur = "Object";
    else return false;                  // 不应发生
  }
}

std::string TypeChecker::lca(const std::string& a, const std::string& b) {
  std::unordered_set<std::string> anc;
  std::string cur = a;
  for (;;) {
    anc.insert(cur);
    if (cur == "Object") break;
    if (parentMap_.count(cur)) cur = parentMap_.at(cur);
    else if (cur == "IO" || cur == "Int" || cur == "String" || cur == "Bool") cur = "Object";
    else break;
  }
  cur = b;
  for (;;) {
    if (anc.count(cur)) return cur;
    if (cur == "Object") return "Object";
    if (parentMap_.count(cur)) cur = parentMap_.at(cur);
    else if (cur == "IO" || cur == "Int" || cur == "String" || cur == "Bool") cur = "Object";
    else return "Object";
  }
}

std::string TypeChecker::lub(const std::string& a, const std::string& b) {
  if (a == b) return a;
  if (a == "SELF_TYPE") return lub(currentClass_, b);
  if (b == "SELF_TYPE") return lub(a, currentClass_);
  return lca(a, b);
}

// ---- 检查类与特性 ----

void TypeChecker::checkClass(const ClassDef& c) {
  currentClass_ = c.name;
  for (auto& f : c.features) checkFeature(*f);
}

void TypeChecker::checkFeature(const Feature& f) {
  O_.pushScope();   // 属性作用域（外层）
  for (auto& kv : attrsOf(currentClass_)) O_.add(kv.first, kv.second);
  O_.pushScope();   // self + 形参（内层）
  O_.add("self", "SELF_TYPE");

  if (auto* a = dynamic_cast<const Attribute*>(&f)) {
    std::string declared = resolveSelf(a->type);
    if (a->init) {
      std::string t = typeOf(*a->init);
      if (!conforms(t, declared))
        error(a->line, "initializer of attribute '" + a->name + "' has type " + t +
                       " but declared type is " + declared);
    }
  } else if (auto* m = dynamic_cast<const Method*>(&f)) {
    for (auto& fm : m->formals) O_.add(fm.name, fm.type);
    std::string t = typeOf(*m->body);
    std::string declared = resolveSelf(m->type);
    if (!conforms(t, declared))
      error(m->line, "body of method '" + m->name + "' has type " + t +
                     " but declared return type is " + declared);
  }

  O_.popScope();
  O_.popScope();
}

// ---- 表达式类型规则 ----

void TypeChecker::visit(const IntConst&)    { result_ = "Int"; }
void TypeChecker::visit(const StringConst&) { result_ = "String"; }
void TypeChecker::visit(const BoolConst&)   { result_ = "Bool"; }

void TypeChecker::visit(const Object& e) {
  auto t = O_.lookup(e.name);
  if (!t) { error(e.line, "undeclared identifier '" + e.name + "'"); result_ = "Object"; return; }
  result_ = *t;
}

void TypeChecker::visit(const Assign& e) {
  if (e.name == "self") { error(e.line, "cannot assign to 'self'"); result_ = "Object"; return; }
  auto t = O_.lookup(e.name);
  if (!t) { error(e.line, "assignment to undeclared identifier '" + e.name + "'"); result_ = "Object"; return; }
  std::string t2 = typeOf(*e.rhs);
  if (!conforms(t2, *t))
    error(e.line, "cannot assign expression of type " + t2 + " to '" + e.name + "' of type " + *t);
  result_ = t2;
}

void TypeChecker::visit(const Dispatch& e) {
  std::string t0 = typeOf(*e.receiver);
  std::string t0p = resolveSelf(t0);
  const auto& menv = methodsOf(t0p);
  auto it = menv.find(e.method);
  if (it == menv.end()) {
    error(e.line, "method '" + e.method + "' not defined in class '" + t0p + "'");
    result_ = "Object"; return;
  }
  const MethodSig& sig = it->second;
  if (sig.argTypes.size() != e.args.size())
    error(e.line, "wrong number of arguments to method '" + e.method + "'");
  for (size_t i = 0; i < e.args.size() && i < sig.argTypes.size(); ++i) {
    std::string ti = typeOf(*e.args[i]);
    if (!conforms(ti, sig.argTypes[i]))
      error(e.args[i]->line, "argument " + std::to_string(i + 1) + " to '" + e.method +
                             "' has type " + ti + " but expected " + sig.argTypes[i]);
  }
  result_ = (sig.retType == "SELF_TYPE") ? t0 : sig.retType;
}

void TypeChecker::visit(const StaticDispatch& e) {
  std::string t0 = typeOf(*e.receiver);
  if (!conforms(t0, e.type))
    error(e.line, "receiver of static dispatch has type " + t0 + " but expected " + e.type);
  const auto& menv = methodsOf(e.type);
  auto it = menv.find(e.method);
  if (it == menv.end()) {
    error(e.line, "method '" + e.method + "' not defined in class '" + e.type + "'");
    result_ = "Object"; return;
  }
  const MethodSig& sig = it->second;
  if (sig.argTypes.size() != e.args.size())
    error(e.line, "wrong number of arguments to method '" + e.method + "'");
  for (size_t i = 0; i < e.args.size() && i < sig.argTypes.size(); ++i) {
    std::string ti = typeOf(*e.args[i]);
    if (!conforms(ti, sig.argTypes[i]))
      error(e.args[i]->line, "argument " + std::to_string(i + 1) + " to '" + e.method +
                             "' has type " + ti + " but expected " + sig.argTypes[i]);
  }
  result_ = (sig.retType == "SELF_TYPE") ? t0 : sig.retType;
}

void TypeChecker::visit(const If& e) {
  std::string tc = typeOf(*e.cond);
  if (tc != "Bool") error(e.cond->line, "if condition must be Bool, got " + tc);
  std::string t2 = typeOf(*e.then_);
  std::string t3 = typeOf(*e.else_);
  result_ = lub(t2, t3);
}

void TypeChecker::visit(const While& e) {
  std::string tc = typeOf(*e.cond);
  if (tc != "Bool") error(e.cond->line, "while condition must be Bool, got " + tc);
  typeOf(*e.body);
  result_ = "Object";
}

void TypeChecker::visit(const Block& e) {
  std::string t = "Object";
  for (auto& x : e.exprs) t = typeOf(*x);
  result_ = t;
}

void TypeChecker::visit(const Let& e) {
  O_.pushScope();
  for (auto& b : e.bindings) {
    if (b.name == "self") { error(e.line, "'self' cannot be bound in a let"); continue; }
    std::string declared = resolveSelf(b.type);
    if (b.init) {
      std::string t = typeOf(*b.init);
      if (!conforms(t, declared))
        error(b.init->line, "let initializer for '" + b.name + "' has type " + t +
                            " but declared type is " + declared);
    }
    O_.add(b.name, b.type);
  }
  result_ = typeOf(*e.body);
  O_.popScope();
}

void TypeChecker::visit(const Case& e) {
  typeOf(*e.scrutinee);
  std::unordered_set<std::string> seen;
  bool first = true;
  std::string joined = "Object";
  for (auto& b : e.branches) {
    if (seen.count(b.type)) {
      error(e.line, "duplicate case branch type '" + b.type + "'");
      continue;
    }
    seen.insert(b.type);
    O_.pushScope();
    O_.add(b.name, b.type);
    std::string t = typeOf(*b.body);
    O_.popScope();
    joined = first ? t : lub(joined, t);
    first = false;
  }
  result_ = joined;
}

void TypeChecker::visit(const New& e) {
  if (e.type == "SELF_TYPE") { result_ = "SELF_TYPE"; return; }
  if (!isType(e.type)) {
    error(e.line, "cannot instantiate undefined type '" + e.type + "'");
    result_ = "Object";
    return;
  }
  result_ = e.type;
}

void TypeChecker::visit(const IsVoid& e) {
  typeOf(*e.expr);
  result_ = "Bool";
}

void TypeChecker::visit(const Binary& e) {
  std::string t1 = typeOf(*e.lhs);
  std::string t2 = typeOf(*e.rhs);
  switch (e.op) {
    case BinOp::Plus: case BinOp::Minus: case BinOp::Mul: case BinOp::Div:
      if (t1 != "Int" || t2 != "Int")
        error(e.line, "arithmetic operands must be Int, got " + t1 + " and " + t2);
      result_ = "Int";
      break;
    case BinOp::Lt: case BinOp::Le: case BinOp::Eq:
      if (t1 == "Int" || t1 == "String" || t1 == "Bool" ||
          t2 == "Int" || t2 == "String" || t2 == "Bool") {
        if (t1 != t2)
          error(e.line, "comparison operands must have the same basic type, got " + t1 + " and " + t2);
      }
      result_ = "Bool";
      break;
  }
}

void TypeChecker::visit(const Neg& e) {
  std::string t = typeOf(*e.expr);
  if (t != "Int") error(e.line, "~ operand must be Int, got " + t);
  result_ = "Int";
}

void TypeChecker::visit(const Not& e) {
  std::string t = typeOf(*e.expr);
  if (t != "Bool") error(e.line, "not operand must be Bool, got " + t);
  result_ = "Bool";
}

void TypeChecker::visit(const NoExpr&) { result_ = "Object"; }

// ---- 入口 ----

bool TypeChecker::check() {
  buildEnvironments();
  if (errorCount_ > 0) return false;
  if (classMap_.count("Main")) {
    const auto& menv = methodsOf("Main");
    auto it = menv.find("main");
    if (it == menv.end()) { error(0, "class 'Main' has no 'main' method"); return false; }
    if (!it->second.argTypes.empty()) { error(0, "'main' method must take no arguments"); return false; }
  }
  for (auto& cls : prog_.classes) checkClass(*cls);
  return errorCount_ == 0;
}

} // namespace cool
