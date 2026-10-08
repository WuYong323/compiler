#include "codegen.h"
#include <cstdio>
#include <unordered_set>

namespace cool {

// ---- 工具 ----

static std::string escapeString(const std::string& s) {
  std::string r;
  for (unsigned char c : s) {
    if (c == '\\') r += "\\\\";
    else if (c == '"') r += "\\22";
    else if (c >= 0x20 && c <= 0x7E) r += (char)c;
    else { char buf[8]; std::snprintf(buf, sizeof buf, "\\%02X", c); r += buf; }
  }
  return r;
}

std::string CodeGen::sizeOf(const std::string& cls) {
  if (cls == "Int" || cls == "Bool") return "16";
  if (cls == "String") return "24";
  return std::to_string((1 + attrs_.at(cls).size()) * 8);
}

int CodeGen::attrOffset(const std::string& cls, const std::string& name) {
  const auto& list = attrs_.at(cls);
  for (size_t i = 0; i < list.size(); ++i)
    if (list[i].name == name) return (int)i;
  return -1;
}

std::string CodeGen::funcType(int arity) const {
  std::string t = "i8* (i8*";
  for (int i = 0; i < arity; ++i) t += ", i8*";
  return t + ")*";
}

// ---- 类信息 ----

void CodeGen::buildClassInfo() {
  const std::vector<std::string> builtins = {"Object", "IO", "Int", "String", "Bool"};
  int id = 0;
  for (auto& b : builtins) classId_[b] = id++;
  parentMap_["Object"] = "";
  parentMap_["IO"] = "Object"; parentMap_["Int"] = "Object";
  parentMap_["String"] = "Object"; parentMap_["Bool"] = "Object";
  for (auto& c : prog_.classes) {
    classMap_[c->name] = c.get();
    classId_[c->name] = id++;
    parentMap_[c->name] = c->parent.empty() ? "Object" : c->parent;
  }
  // 后代集合（自身 + 所有子类）
  for (auto& kv : classId_) descendants_[kv.first] = {kv.first};
  for (auto& kv : parentMap_) {
    std::string cur = kv.second;
    while (!cur.empty()) { descendants_[cur].push_back(kv.first); cur = parentMap_[cur]; }
  }
  // 方法环境（内建 + 程序类，拓扑序）
  methods_["Object"] = {{"abort", {"@Object.abort", 0}},
                        {"type_name", {"@Object.type_name", 0}},
                        {"copy", {"@Object.copy", 0}}};
  methods_["IO"] = methods_["Object"];
  methods_["IO"]["out_string"] = {"@IO.out_string", 1};
  methods_["IO"]["out_int"] = {"@IO.out_int", 1};
  methods_["IO"]["in_string"] = {"@IO.in_string", 0};
  methods_["IO"]["in_int"] = {"@IO.in_int", 0};
  methods_["Int"] = methods_["Object"];
  methods_["String"] = methods_["Object"];
  methods_["String"]["length"] = {"@String.length", 0};
  methods_["String"]["concat"] = {"@String.concat", 1};
  methods_["String"]["substr"] = {"@String.substr", 2};
  methods_["Bool"] = methods_["Object"];
  std::unordered_set<std::string> done(builtins.begin(), builtins.end());
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto& c : prog_.classes) {
      if (done.count(c->name)) continue;
      if (!done.count(parentMap_[c->name])) continue;
      methods_[c->name] = methods_[parentMap_[c->name]];
      for (auto& f : c->features)
        if (auto* m = dynamic_cast<const Method*>(f.get()))
          methods_[c->name][m->name] = {"@" + c->name + "." + m->name, (int)m->formals.size()};
      done.insert(c->name);
      progress = true;
    }
  }
  // 方法下标（全局名字 -> 下标）
  auto assign = [&](const std::string& n) { if (!methodIndex_.count(n)) methodIndex_[n] = nextMethodIndex_++; };
  for (auto& b : builtins)
    for (auto& kv : methods_[b]) assign(kv.first);
  for (auto& c : prog_.classes)
    for (auto& kv : methods_[c->name]) assign(kv.first);
  // 属性槽（继承 + 自身，有序）
  for (auto& b : builtins) attrs_[b] = {};
  std::unordered_set<std::string> doneA(builtins.begin(), builtins.end());
  progress = true;
  while (progress) {
    progress = false;
    for (auto& c : prog_.classes) {
      if (doneA.count(c->name)) continue;
      if (!doneA.count(parentMap_[c->name])) continue;
      attrs_[c->name] = attrs_[parentMap_[c->name]];
      for (auto& f : c->features)
        if (auto* a = dynamic_cast<const Attribute*>(f.get()))
          attrs_[c->name].push_back({a->name, a->type, a->init.get()});
      doneA.insert(c->name);
      progress = true;
    }
  }
}

// ---- 常量 ----

std::string CodeGen::intConst(int v) {
  auto it = intConsts_.find(v);
  if (it != intConsts_.end()) return it->second;
  std::string g = "@int_" + std::to_string(v);
  globals_ += g + " = constant %Int { i8* bitcast (%class_meta_t* @class_Int to i8*), i32 " +
              std::to_string(v) + " }\n";
  intConsts_[v] = g;
  return "bitcast (%Int* " + g + " to i8*)";
}

std::string CodeGen::strConst(const std::string& lexeme) {
  for (size_t i = 0; i < stringConsts_.size(); ++i)
    if (stringConsts_[i] == lexeme)
      return "bitcast (%String* @strlit_" + std::to_string(i) + " to i8*)";
  size_t idx = stringConsts_.size();
  stringConsts_.push_back(lexeme);
  std::string esc = escapeString(lexeme);
  size_t len = lexeme.size();
  globals_ += "@strlit_" + std::to_string(idx) + "_chars = private constant [" + std::to_string(len + 1) +
       " x i8] c\"" + esc + "\\00\"\n";
  globals_ += "@strlit_" + std::to_string(idx) + " = constant %String { i8* bitcast (%class_meta_t* @class_String to i8*), i32 " +
       std::to_string(len) + ", i8* getelementptr inbounds ([" + std::to_string(len + 1) + " x i8], [" +
       std::to_string(len + 1) + " x i8]* @strlit_" + std::to_string(idx) + "_chars, i32 0, i32 0) }\n";
  return "bitcast (%String* @strlit_" + std::to_string(idx) + " to i8*)";
}

// ---- 运行时错误守卫 ----

std::string CodeGen::msgConst(const std::string& msg) {
  auto it = msgConsts_.find(msg);
  if (it != msgConsts_.end()) return it->second;
  size_t idx = msgConsts_.size();
  std::string g = "@rterr_" + std::to_string(idx);
  size_t len = msg.size() + 1;
  globals_ += g + " = private constant [" + std::to_string(len) + " x i8] c\"" +
              escapeString(msg) + "\\00\"\n";
  std::string expr = "getelementptr inbounds ([" + std::to_string(len) + " x i8], [" +
                     std::to_string(len) + " x i8]* " + g + ", i32 0, i32 0)";
  msgConsts_[msg] = expr;
  return expr;
}

// cond 为真 => 报错退出。切分出 fail/ok 两块，ok 成为新的 currentBlock_。
void CodeGen::emitGuard(const std::string& i1cond, const std::string& msg) {
  std::string failL = freshLabel(), okL = freshLabel();
  emit("br i1 " + i1cond + ", label %" + failL + ", label %" + okL);
  emitLabel(failL);
  emit("call void @cool_rt_error(i8* " + msgConst(msg) + ")");
  emit("unreachable");
  emitLabel(okL);
}

void CodeGen::guardVoid(const std::string& v, const std::string& msg) {
  std::string c = fresh();
  emit(c + " = icmp eq i8* " + v + ", null");
  emitGuard(c, msg);
}

std::string CodeGen::defaultFor(const std::string& type) {
  if (type == "Int") return intConst(0);
  if (type == "String") return "bitcast (%String* @strlit_empty to i8*)";
  if (type == "Bool") return "bitcast (%Bool* @bool_false to i8*)";
  return "null";
}

// ---- 装箱/拆箱 ----

std::string CodeGen::unboxInt(const std::string& v) {
  std::string p = fresh(); emit(p + " = bitcast i8* " + v + " to %Int*");
  std::string f = fresh(); emit(f + " = getelementptr %Int, %Int* " + p + ", i32 0, i32 1");
  std::string r = fresh(); emit(r + " = load i32, i32* " + f);
  return r;
}
std::string CodeGen::unboxBool(const std::string& v) {
  std::string p = fresh(); emit(p + " = bitcast i8* " + v + " to %Bool*");
  std::string f = fresh(); emit(f + " = getelementptr %Bool, %Bool* " + p + ", i32 0, i32 1");
  std::string r = fresh(); emit(r + " = load i32, i32* " + f);
  return r;
}
std::string CodeGen::unboxStrChars(const std::string& v) {
  std::string p = fresh(); emit(p + " = bitcast i8* " + v + " to %String*");
  std::string f = fresh(); emit(f + " = getelementptr %String, %String* " + p + ", i32 0, i32 2");
  std::string r = fresh(); emit(r + " = load i8*, i8** " + f);
  return r;
}
std::string CodeGen::boxInt(const std::string& i32v) {
  std::string obj = fresh(); emit(obj + " = call i8* @cool_alloc(i32 16)");
  std::string p = fresh(); emit(p + " = bitcast i8* " + obj + " to %Int*");
  std::string tf = fresh(); emit(tf + " = getelementptr %Int, %Int* " + p + ", i32 0, i32 0");
  emit("store i8* bitcast (%class_meta_t* @class_Int to i8*), i8** " + tf);
  std::string vf = fresh(); emit(vf + " = getelementptr %Int, %Int* " + p + ", i32 0, i32 1");
  emit("store i32 " + i32v + ", i32* " + vf);
  return obj;
}
// 装箱为 Bool 对象：标签必须是 @class_Bool，否则 type_name()/case 会把 Bool 误认成 Int
std::string CodeGen::boxBool(const std::string& i32v) {
  std::string obj = fresh(); emit(obj + " = call i8* @cool_alloc(i32 16)");
  std::string p = fresh(); emit(p + " = bitcast i8* " + obj + " to %Bool*");
  std::string tf = fresh(); emit(tf + " = getelementptr %Bool, %Bool* " + p + ", i32 0, i32 0");
  emit("store i8* bitcast (%class_meta_t* @class_Bool to i8*), i8** " + tf);
  std::string vf = fresh(); emit(vf + " = getelementptr %Bool, %Bool* " + p + ", i32 0, i32 1");
  emit("store i32 " + i32v + ", i32* " + vf);
  return obj;
}
std::string CodeGen::wrapStr(const std::string& chars) {
  std::string obj = fresh(); emit(obj + " = call i8* @cool_alloc(i32 24)");
  std::string p = fresh(); emit(p + " = bitcast i8* " + obj + " to %String*");
  std::string tf = fresh(); emit(tf + " = getelementptr %String, %String* " + p + ", i32 0, i32 0");
  emit("store i8* bitcast (%class_meta_t* @class_String to i8*), i8** " + tf);
  std::string len = fresh(); emit(len + " = call i32 @str_len(i8* " + chars + ")");
  std::string lf = fresh(); emit(lf + " = getelementptr %String, %String* " + p + ", i32 0, i32 1");
  emit("store i32 " + len + ", i32* " + lf);
  std::string cf = fresh(); emit(cf + " = getelementptr %String, %String* " + p + ", i32 0, i32 2");
  emit("store i8* " + chars + ", i8** " + cf);
  return obj;
}

// ---- 表达式代码生成 ----

std::string CodeGen::codegenExpr(const Expr& e) { e.accept(*this); return result_; }

void CodeGen::visit(const IntConst& e)    { result_ = intConst(e.value); }
void CodeGen::visit(const StringConst& e) { result_ = strConst(e.value); }
void CodeGen::visit(const BoolConst& e) {
  result_ = e.value ? "bitcast (%Bool* @bool_true to i8*)" : "bitcast (%Bool* @bool_false to i8*)";
}
void CodeGen::visit(const NoExpr&) { result_ = "null"; }

void CodeGen::visit(const Object& e) {
  // 局部变量？
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto f = it->find(e.name);
    if (f != it->end()) {
      std::string r = fresh(); emit(r + " = load i8*, i8** " + f->second);
      result_ = r; return;
    }
  }
  // 属性：load self, gep, load
  std::string self = fresh(); emit(self + " = load i8*, i8** " + selfSlot());
  std::string casted = fresh(); emit(casted + " = bitcast i8* " + self + " to %" + currentClass_ + "*");
  int off = attrOffset(currentClass_, e.name);
  std::string f = fresh();
  emit(f + " = getelementptr %" + currentClass_ + ", %" + currentClass_ + "* " + casted +
       ", i32 0, i32 " + std::to_string(off + 1));
  std::string r = fresh(); emit(r + " = load i8*, i8** " + f);
  result_ = r;
}

void CodeGen::visit(const Assign& e) {
  std::string rhs = codegenExpr(*e.rhs);
  bool isLocal = false;
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto f = it->find(e.name);
    if (f != it->end()) { emit("store i8* " + rhs + ", i8** " + f->second); isLocal = true; break; }
  }
  if (!isLocal) {
    std::string self = fresh(); emit(self + " = load i8*, i8** " + selfSlot());
    std::string casted = fresh(); emit(casted + " = bitcast i8* " + self + " to %" + currentClass_ + "*");
    int off = attrOffset(currentClass_, e.name);
    std::string f = fresh();
    emit(f + " = getelementptr %" + currentClass_ + ", %" + currentClass_ + "* " + casted +
         ", i32 0, i32 " + std::to_string(off + 1));
    emit("store i8* " + rhs + ", i8** " + f);
  }
  result_ = rhs;
}

void CodeGen::visit(const Dispatch& e) {
  // 手册 :265 规定的顺序：**先**从左到右求实参 e1..en，**再**求接收者 e0，
  // 然后才查 e0 是否为 void。反过来写会让带副作用的接收者与实参顺序颠倒。
  std::vector<std::string> args;
  for (auto& a : e.args) args.push_back(codegenExpr(*a));
  std::string recv = codegenExpr(*e.receiver);
  int idx = methodIndex_.at(e.method);
  // 手册 §13.1：对 void 分派是运行时错误。不查就是解引用 null。
  guardVoid(recv, "dispatch on void");
  // tag -> meta -> vtable -> fn
  std::string tag = fresh(); emit(tag + " = load i8*, i8** " + recv);
  std::string meta = fresh(); emit(meta + " = bitcast i8* " + tag + " to %class_meta_t*");
  std::string vtf = fresh(); emit(vtf + " = getelementptr %class_meta_t, %class_meta_t* " + meta + ", i32 0, i32 1");
  std::string vtp = fresh(); emit(vtp + " = load i8**, i8*** " + vtf);
  std::string slot = fresh(); emit(slot + " = getelementptr i8*, i8** " + vtp + ", i32 " + std::to_string(idx));
  std::string fnp = fresh(); emit(fnp + " = load i8*, i8** " + slot);
  std::string fn = fresh(); emit(fn + " = bitcast i8* " + fnp + " to " + funcType((int)args.size()));
  std::string ca = "i8* " + recv;
  for (auto& a : args) ca += ", i8* " + a;
  std::string r = fresh(); emit(r + " = call i8* " + fn + "(" + ca + ")");
  result_ = r;
}

void CodeGen::visit(const StaticDispatch& e) {
  // 同 visit(Dispatch)：手册 :265 的顺序是先实参、后接收者。
  std::vector<std::string> args;
  for (auto& a : e.args) args.push_back(codegenExpr(*a));
  std::string recv = codegenExpr(*e.receiver);
  // 手册 §13.1：静态分派对 void 同样是运行时错误。
  guardVoid(recv, "static dispatch on void");
  std::string ca = "i8* " + recv;
  for (auto& a : args) ca += ", i8* " + a;
  std::string r = fresh();
  // 不能用 "@" + e.type + "." + e.method 拼函数名：若 e.type 只是继承了该方法而没有
  // 定义它，那个函数并不存在（会生成 call @B.f 而只有 @A.f 被定义）。
  // methods_ 已按继承链解析好，查表即可（存在性由 typechecker 保证）。
  const MethodRef& ref = methods_.at(e.type).at(e.method);
  emit(r + " = call i8* " + ref.funcName + "(" + ca + ")");
  result_ = r;
}

void CodeGen::visit(const If& e) {
  std::string ci = unboxBool(codegenExpr(*e.cond));
  std::string b = fresh(); emit(b + " = icmp ne i32 " + ci + ", 0");
  std::string thenL = freshLabel(), elseL = freshLabel(), endL = freshLabel();
  emit("br i1 " + b + ", label %" + thenL + ", label %" + elseL);
  emitLabel(thenL);
  std::string tv = codegenExpr(*e.then_);
  std::string thenExit = currentBlock_;   // then 支可能自己开了新块
  emit("br label %" + endL);
  emitLabel(elseL);
  std::string ev = codegenExpr(*e.else_);
  std::string elseExit = currentBlock_;   // else 支同理
  emit("br label %" + endL);
  emitLabel(endL);
  std::string phi = fresh();
  emit(phi + " = phi i8* [ " + tv + ", %" + thenExit + " ], [ " + ev + ", %" + elseExit + " ]");
  result_ = phi;
}

void CodeGen::visit(const While& e) {
  std::string condL = freshLabel(), bodyL = freshLabel(), endL = freshLabel();
  emit("br label %" + condL);
  emitLabel(condL);
  std::string ci = unboxBool(codegenExpr(*e.cond));
  std::string b = fresh(); emit(b + " = icmp ne i32 " + ci + ", 0");
  emit("br i1 " + b + ", label %" + bodyL + ", label %" + endL);
  emitLabel(bodyL);
  codegenExpr(*e.body);
  emit("br label %" + condL);
  emitLabel(endL);
  result_ = "null";
}

void CodeGen::visit(const Block& e) {
  std::string r = "null";
  for (auto& x : e.exprs) r = codegenExpr(*x);
  result_ = r;
}

void CodeGen::visit(const Let& e) {
  scopes_.push_back({});
  for (auto& b : e.bindings) {
    std::string slot = fresh(); emit(slot + " = alloca i8*");
    std::string val = b.init ? codegenExpr(*b.init) : defaultFor(b.type);
    emit("store i8* " + val + ", i8** " + slot);
    scopes_.back()[b.name] = slot;
  }
  result_ = codegenExpr(*e.body);
  scopes_.pop_back();
}

void CodeGen::visit(const Case& e) {
  std::string scrut = codegenExpr(*e.scrutinee);
  // 手册 §13.2：case on void 是运行时错误。
  guardVoid(scrut, "case on void");
  std::string tag = fresh(); emit(tag + " = load i8*, i8** " + scrut);
  std::string meta = fresh(); emit(meta + " = bitcast i8* " + tag + " to %class_meta_t*");
  std::string idf = fresh(); emit(idf + " = getelementptr %class_meta_t, %class_meta_t* " + meta + ", i32 0, i32 3");
  std::string id = fresh(); emit(id + " = load i32, i32* " + idf);

  std::string endL = freshLabel();
  std::vector<std::pair<std::string, std::string>> results;
  std::string checkL = freshLabel();
  emit("br label %" + checkL);
  for (auto& br : e.branches) {
    emitLabel(checkL);
    const auto& desc = descendants_.at(br.type);
    std::string m;
    for (size_t d = 0; d < desc.size(); ++d) {
      std::string c = fresh();
      emit(c + " = icmp eq i32 " + id + ", " + std::to_string(classId_.at(desc[d])));
      if (d == 0) m = c;
      else { std::string o = fresh(); emit(o + " = or i1 " + m + ", " + c); m = o; }
    }
    std::string bodyL = freshLabel();
    std::string nextL = freshLabel();
    emit("br i1 " + m + ", label %" + bodyL + ", label %" + nextL);
    checkL = nextL;
    emitLabel(bodyL);
    scopes_.push_back({});
    std::string slot = fresh(); emit(slot + " = alloca i8*");
    emit("store i8* " + scrut + ", i8** " + slot);
    scopes_.back()[br.name] = slot;
    std::string r = codegenExpr(*br.body);
    scopes_.pop_back();
    std::string bodyExit = currentBlock_;   // 分支体可能自己开了新块
    emit("br label %" + endL);
    results.push_back({bodyExit, r});
  }
  emitLabel(checkL);
  emit("call void @cool_abort()");
  emit("unreachable");
  emitLabel(endL);
  std::string phi = fresh();
  std::string args;
  for (auto& p : results) { if (!args.empty()) args += ", "; args += "[ " + p.second + ", %" + p.first + " ]"; }
  if (args.empty()) args = "null";
  emit(phi + " = phi i8* " + args);
  result_ = phi;
}

void CodeGen::visit(const New& e) {
  result_ = allocateAndInit(e.type == "SELF_TYPE" ? currentClass_ : e.type);
}

void CodeGen::visit(const IsVoid& e) {
  std::string v = codegenExpr(*e.expr);
  std::string c = fresh(); emit(c + " = icmp eq i8* " + v + ", null");
  std::string r = fresh(); emit(r + " = zext i1 " + c + " to i32");
  result_ = boxBool(r);
}

void CodeGen::visit(const Binary& e) {
  if (e.op == BinOp::Plus || e.op == BinOp::Minus || e.op == BinOp::Mul || e.op == BinOp::Div) {
    std::string a = unboxInt(codegenExpr(*e.lhs));
    std::string b = unboxInt(codegenExpr(*e.rhs));
    const char* op = e.op == BinOp::Plus ? "add" : e.op == BinOp::Minus ? "sub" :
                     e.op == BinOp::Mul ? "mul" : "sdiv";
    // 手册 §13.4：除零是运行时错误。LLVM 的 sdiv 除以 0 是 UB（实测直接 SIGFPE/127，
    // 不走任何清理路径），所以必须在指令之前自己拦。
    if (e.op == BinOp::Div) {
      std::string z = fresh(); emit(z + " = icmp eq i32 " + b + ", 0");
      emitGuard(z, "division by zero");
    }
    std::string r = fresh(); emit(r + " = " + op + " i32 " + a + ", " + b);
    result_ = boxInt(r);
  } else {  // < <= =
    std::string lhs = codegenExpr(*e.lhs), rhs = codegenExpr(*e.rhs);
    std::string t = e.lhs->type;
    std::string i1v;
    if (t == "Int") {
      std::string a = unboxInt(lhs), b = unboxInt(rhs);
      const char* pred = e.op == BinOp::Lt ? "slt" : e.op == BinOp::Le ? "sle" : "eq";
      i1v = fresh(); emit(i1v + " = icmp " + pred + " i32 " + a + ", " + b);
    } else if (t == "Bool") {
      std::string a = unboxBool(lhs), b = unboxBool(rhs);
      const char* pred = e.op == BinOp::Lt ? "slt" : e.op == BinOp::Le ? "sle" : "eq";
      i1v = fresh(); emit(i1v + " = icmp " + pred + " i32 " + a + ", " + b);
    } else if (t == "String") {
      // 用运行时 str_cmp 比较（<, <=, =）
      std::string a = unboxStrChars(lhs), b = unboxStrChars(rhs);
      std::string cmp = fresh(); emit(cmp + " = call i32 @str_cmp(i8* " + a + ", i8* " + b + ")");
      const char* pred = e.op == BinOp::Lt ? "slt" : e.op == BinOp::Le ? "sle" : "eq";
      i1v = fresh(); emit(i1v + " = icmp " + pred + " i32 " + cmp + ", 0");
    } else {
      // 非基本类型：指针相等
      const char* pred = e.op == BinOp::Eq ? "eq" : "ne";  // < <= 对任意对象运行时错误，简化为 ne（不该发生）
      i1v = fresh(); emit(i1v + " = icmp " + pred + " i8* " + lhs + ", " + rhs);
    }
    std::string r = fresh(); emit(r + " = zext i1 " + i1v + " to i32");
    result_ = boxBool(r);
  }
}

void CodeGen::visit(const Neg& e) {
  std::string a = unboxInt(codegenExpr(*e.expr));
  std::string r = fresh(); emit(r + " = sub i32 0, " + a);
  result_ = boxInt(r);
}

void CodeGen::visit(const Not& e) {
  std::string a = unboxBool(codegenExpr(*e.expr));
  std::string r = fresh(); emit(r + " = xor i32 " + a + ", 1");
  result_ = boxBool(r);
}

// ---- new 对象分配与初始化 ----

std::string CodeGen::allocateAndInit(const std::string& cls) {
  std::string obj = fresh(); emit(obj + " = call i8* @cool_alloc(i32 " + sizeOf(cls) + ")");
  std::string casted = fresh(); emit(casted + " = bitcast i8* " + obj + " to %" + cls + "*");
  std::string tf = fresh();
  emit(tf + " = getelementptr %" + cls + ", %" + cls + "* " + casted + ", i32 0, i32 0");
  emit("store i8* bitcast (%class_meta_t* @class_" + cls + " to i8*), i8** " + tf);
  std::string saved = currentClass_;
  currentClass_ = cls;
  scopes_.push_back({});
  std::string selfA = fresh(); emit(selfA + " = alloca i8*");
  emit("store i8* " + obj + ", i8** " + selfA);
  scopes_.back()["self"] = selfA;
  // 手册 :730/:781：求任何初始化式**之前**，所有属性先绑到各自类型的默认值
  // （Int=0 / String="" / Bool=false / 其他=void）。少了这一趟，初始化式读一个
  // 写在它后面的属性就会读到 malloc 的未初始化内存——实测直接段错误。
  for (auto& slot : attrs_.at(cls)) {
    int off = attrOffset(cls, slot.name);
    std::string f = fresh();
    emit(f + " = getelementptr %" + cls + ", %" + cls + "* " + casted + ", i32 0, i32 " + std::to_string(off + 1));
    emit("store i8* " + defaultFor(slot.type) + ", i8** " + f);
  }
  // 第二趟才求初始化式（手册 :727：先祖先后子类，同类内按源码顺序；attrs_ 就是这个序）
  for (auto& slot : attrs_.at(cls)) {
    if (!slot.init) continue;
    std::string val = codegenExpr(*slot.init);
    int off = attrOffset(cls, slot.name);
    std::string f = fresh();
    emit(f + " = getelementptr %" + cls + ", %" + cls + "* " + casted + ", i32 0, i32 " + std::to_string(off + 1));
    emit("store i8* " + val + ", i8** " + f);
  }
  scopes_.pop_back();
  currentClass_ = saved;
  return obj;
}

// ---- 各类定义 ----

void CodeGen::emitClassMeta(const ClassDef& c) {
  const std::string& name = c.name;
  std::string esc = escapeString(name);
  emit("@str_" + name + " = private constant [" + std::to_string(name.size() + 1) + " x i8] c\"" + esc + "\\00\"");
  std::string vt = "@vtable_" + name + " = constant [" + std::to_string(nextMethodIndex_) + " x i8*] [";
  std::vector<std::string> slots(nextMethodIndex_, "i8* null");
  for (auto& kv : methods_.at(name)) {
    const MethodRef& ref = kv.second;
    slots[methodIndex_.at(kv.first)] = "i8* bitcast (" + funcType(ref.arity) + " " + ref.funcName + " to i8*)";
  }
  for (size_t i = 0; i < slots.size(); ++i) { if (i) vt += ", "; vt += slots[i]; }
  vt += "]";
  emit(vt);
  emit("@class_" + name + " = constant %class_meta_t { i8* getelementptr inbounds ([" +
       std::to_string(name.size() + 1) + " x i8], [" + std::to_string(name.size() + 1) + " x i8]* @str_" +
       name + ", i32 0, i32 0), i8** bitcast ([" + std::to_string(nextMethodIndex_) + " x i8*]* @vtable_" +
       name + " to i8**), i32 " + sizeOf(name) + ", i32 " + std::to_string(classId_.at(name)) + " }");
}

void CodeGen::emitPrelude() {
  emit("; 类型定义");
  emit("%Int = type { i8*, i32 }");
  emit("%Bool = type { i8*, i32 }");
  emit("%String = type { i8*, i32, i8* }");
  emit("%class_meta_t = type { i8*, i8**, i32, i32 }");
  // 无属性的内建类（Object/IO）也要有结构体类型，否则 new Object / new IO 会产生未定义类型
  emit("%Object = type { i8* }");
  emit("%IO = type { i8* }");
  // 用户类的结构体类型
  for (auto& c : prog_.classes) {
    std::string s = "%" + c->name + " = type { i8*";
    size_t n = attrs_.at(c->name).size();
    for (size_t i = 0; i < n; ++i) s += ", i8*";
    s += " }";
    emit(s);
  }
  emit("");
  emit("; 外部运行时声明");
  emit("declare i8* @cool_alloc(i32)");
  emit("declare i8* @cool_copy(i8*, i32)");
  emit("declare void @print_int(i32)");
  emit("declare void @print_str(i8*)");
  emit("declare i32 @read_int()");
  emit("declare i8* @read_str()");
  emit("declare i32 @str_len(i8*)");
  emit("declare i8* @str_concat(i8*, i8*)");
  emit("declare i8* @str_substr(i8*, i32, i32)");
  emit("declare i32 @str_cmp(i8*, i8*)");
  emit("declare void @cool_abort()");
  emit("declare void @cool_rt_error(i8*)");
  emit("");
  emit("; 内建类元信息");
  // Object / IO / Int / String / Bool
  for (auto* n : {"Object", "IO", "Int", "String", "Bool"}) {
    std::string name = n;
    emit("@str_" + name + " = private constant [" + std::to_string(name.size() + 1) + " x i8] c\"" + escapeString(name) + "\\00\"");
    std::string vt = "@vtable_" + name + " = constant [" + std::to_string(nextMethodIndex_) + " x i8*] [";
    std::vector<std::string> slots(nextMethodIndex_, "i8* null");
    for (auto& kv : methods_.at(name)) {
      const MethodRef& ref = kv.second;
      slots[methodIndex_.at(kv.first)] = "i8* bitcast (" + funcType(ref.arity) + " " + ref.funcName + " to i8*)";
    }
    for (size_t i = 0; i < slots.size(); ++i) { if (i) vt += ", "; vt += slots[i]; }
    vt += "]";
    emit(vt);
    emit("@class_" + name + " = constant %class_meta_t { i8* getelementptr inbounds ([" +
         std::to_string(name.size() + 1) + " x i8], [" + std::to_string(name.size() + 1) + " x i8]* @str_" +
         name + ", i32 0, i32 0), i8** bitcast ([" + std::to_string(nextMethodIndex_) + " x i8*]* @vtable_" +
         name + " to i8**), i32 " + sizeOf(name) + ", i32 " + std::to_string(classId_.at(name)) + " }");
  }
  emit("");
  emit("; 布尔常量");
  emit("@bool_true = constant %Bool { i8* bitcast (%class_meta_t* @class_Bool to i8*), i32 1 }");
  emit("@bool_false = constant %Bool { i8* bitcast (%class_meta_t* @class_Bool to i8*), i32 0 }");
  emit("; 空字符串");
  emit("@strlit_empty_chars = private constant [1 x i8] c\"\\00\"");
  emit("@strlit_empty = constant %String { i8* bitcast (%class_meta_t* @class_String to i8*), i32 0, i8* getelementptr inbounds ([1 x i8], [1 x i8]* @strlit_empty_chars, i32 0, i32 0) }");
  emit("");
}

void CodeGen::emitMethod(const std::string& cls, const Method& m) {
  currentClass_ = cls;
  std::string sig = "define i8* @" + cls + "." + m.name + "(i8* %self_arg";
  for (size_t i = 0; i < m.formals.size(); ++i) sig += ", i8* %arg_" + std::to_string(i);
  sig += ") {";
  emit(sig);
  emitLabel("entry");
  emit("%self = alloca i8*");
  emit("store i8* %self_arg, i8** %self");
  scopes_.push_back({});
  scopes_.back()["self"] = "%self";
  for (size_t i = 0; i < m.formals.size(); ++i) {
    std::string slot = fresh(); emit(slot + " = alloca i8*");
    emit("store i8* %arg_" + std::to_string(i) + ", i8** " + slot);
    scopes_.back()[m.formals[i].name] = slot;
  }
  std::string r = codegenExpr(*m.body);
  emit("ret i8* " + r);
  scopes_.pop_back();
  emit("}");
  emit("");
}

void CodeGen::emitBuiltinMethods() {
  emit("; ===== 内建方法 =====");
  emit("define i8* @Object.abort(i8* %self_arg) {");
  emit("entry:");
  emit("  call void @cool_abort()");
  emit("  ret i8* null");
  emit("}");
  emit("");
  emit("define i8* @Object.type_name(i8* %self_arg) {");
  emit("entry:");
  emit("  %t = load i8*, i8** %self_arg");
  emit("  %m = bitcast i8* %t to %class_meta_t*");
  emit("  %nf = getelementptr %class_meta_t, %class_meta_t* %m, i32 0, i32 0");
  emit("  %name = load i8*, i8** %nf");
  emit("  %obj = call i8* @cool_alloc(i32 24)");
  emit("  %s = bitcast i8* %obj to %String*");
  emit("  %tf = getelementptr %String, %String* %s, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_String to i8*), i8** %tf");
  emit("  %len = call i32 @str_len(i8* %name)");
  emit("  %lf = getelementptr %String, %String* %s, i32 0, i32 1");
  emit("  store i32 %len, i32* %lf");
  emit("  %cf = getelementptr %String, %String* %s, i32 0, i32 2");
  emit("  store i8* %name, i8** %cf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
  emit("define i8* @Object.copy(i8* %self_arg) {");
  emit("entry:");
  emit("  %t = load i8*, i8** %self_arg");
  emit("  %m = bitcast i8* %t to %class_meta_t*");
  emit("  %sf = getelementptr %class_meta_t, %class_meta_t* %m, i32 0, i32 2");
  emit("  %sz = load i32, i32* %sf");
  emit("  %cp = call i8* @cool_copy(i8* %self_arg, i32 %sz)");
  emit("  ret i8* %cp");
  emit("}");
  emit("");
  emit("define i8* @IO.out_string(i8* %self_arg, i8* %x) {");
  emit("entry:");
  emit("  %s = bitcast i8* %x to %String*");
  emit("  %cf = getelementptr %String, %String* %s, i32 0, i32 2");
  emit("  %c = load i8*, i8** %cf");
  emit("  call void @print_str(i8* %c)");
  emit("  ret i8* %self_arg");
  emit("}");
  emit("");
  emit("define i8* @IO.out_int(i8* %self_arg, i8* %x) {");
  emit("entry:");
  emit("  %i = bitcast i8* %x to %Int*");
  emit("  %vf = getelementptr %Int, %Int* %i, i32 0, i32 1");
  emit("  %v = load i32, i32* %vf");
  emit("  call void @print_int(i32 %v)");
  emit("  ret i8* %self_arg");
  emit("}");
  emit("");
  emit("define i8* @IO.in_string(i8* %self_arg) {");
  emit("entry:");
  emit("  %c = call i8* @read_str()");
  emit("  %obj = call i8* @cool_alloc(i32 24)");
  emit("  %s = bitcast i8* %obj to %String*");
  emit("  %tf = getelementptr %String, %String* %s, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_String to i8*), i8** %tf");
  emit("  %len = call i32 @str_len(i8* %c)");
  emit("  %lf = getelementptr %String, %String* %s, i32 0, i32 1");
  emit("  store i32 %len, i32* %lf");
  emit("  %cf = getelementptr %String, %String* %s, i32 0, i32 2");
  emit("  store i8* %c, i8** %cf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
  emit("define i8* @IO.in_int(i8* %self_arg) {");
  emit("entry:");
  emit("  %v = call i32 @read_int()");
  emit("  %obj = call i8* @cool_alloc(i32 16)");
  emit("  %i = bitcast i8* %obj to %Int*");
  emit("  %tf = getelementptr %Int, %Int* %i, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_Int to i8*), i8** %tf");
  emit("  %vf = getelementptr %Int, %Int* %i, i32 0, i32 1");
  emit("  store i32 %v, i32* %vf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
  emit("define i8* @String.length(i8* %self_arg) {");
  emit("entry:");
  emit("  %s = bitcast i8* %self_arg to %String*");
  emit("  %lf = getelementptr %String, %String* %s, i32 0, i32 1");
  emit("  %len = load i32, i32* %lf");
  emit("  %obj = call i8* @cool_alloc(i32 16)");
  emit("  %i = bitcast i8* %obj to %Int*");
  emit("  %tf = getelementptr %Int, %Int* %i, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_Int to i8*), i8** %tf");
  emit("  %vf = getelementptr %Int, %Int* %i, i32 0, i32 1");
  emit("  store i32 %len, i32* %vf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
  emit("define i8* @String.concat(i8* %self_arg, i8* %s_arg) {");
  emit("entry:");
  emit("  %a = bitcast i8* %self_arg to %String*");
  emit("  %af = getelementptr %String, %String* %a, i32 0, i32 2");
  emit("  %ac = load i8*, i8** %af");
  emit("  %b = bitcast i8* %s_arg to %String*");
  emit("  %bf = getelementptr %String, %String* %b, i32 0, i32 2");
  emit("  %bc = load i8*, i8** %bf");
  emit("  %cc = call i8* @str_concat(i8* %ac, i8* %bc)");
  emit("  %obj = call i8* @cool_alloc(i32 24)");
  emit("  %s = bitcast i8* %obj to %String*");
  emit("  %tf = getelementptr %String, %String* %s, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_String to i8*), i8** %tf");
  emit("  %len = call i32 @str_len(i8* %cc)");
  emit("  %lf = getelementptr %String, %String* %s, i32 0, i32 1");
  emit("  store i32 %len, i32* %lf");
  emit("  %cf = getelementptr %String, %String* %s, i32 0, i32 2");
  emit("  store i8* %cc, i8** %cf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
  emit("define i8* @String.substr(i8* %self_arg, i8* %i_arg, i8* %l_arg) {");
  emit("entry:");
  emit("  %a = bitcast i8* %self_arg to %String*");
  emit("  %af = getelementptr %String, %String* %a, i32 0, i32 2");
  emit("  %ac = load i8*, i8** %af");
  emit("  %ii = bitcast i8* %i_arg to %Int*");
  emit("  %ivf = getelementptr %Int, %Int* %ii, i32 0, i32 1");
  emit("  %iv = load i32, i32* %ivf");
  emit("  %ll = bitcast i8* %l_arg to %Int*");
  emit("  %lvf = getelementptr %Int, %Int* %ll, i32 0, i32 1");
  emit("  %lv = load i32, i32* %lvf");
  emit("  %cc = call i8* @str_substr(i8* %ac, i32 %iv, i32 %lv)");
  emit("  %obj = call i8* @cool_alloc(i32 24)");
  emit("  %s = bitcast i8* %obj to %String*");
  emit("  %tf = getelementptr %String, %String* %s, i32 0, i32 0");
  emit("  store i8* bitcast (%class_meta_t* @class_String to i8*), i8** %tf");
  emit("  %len = call i32 @str_len(i8* %cc)");
  emit("  %lf = getelementptr %String, %String* %s, i32 0, i32 1");
  emit("  store i32 %len, i32* %lf");
  emit("  %cf = getelementptr %String, %String* %s, i32 0, i32 2");
  emit("  store i8* %cc, i8** %cf");
  emit("  ret i8* %obj");
  emit("}");
  emit("");
}

void CodeGen::emitMain() {
  emit("define i32 @main() {");
  emitLabel("entry");
  std::string obj = allocateAndInit("Main");
  const MethodRef& mref = methods_.at("Main").at("main");
  std::string r = fresh();
  emit(r + " = call i8* " + mref.funcName + "(i8* " + obj + ")");
  emit("ret i32 0");
  emit("}");
}

std::string CodeGen::generate() {
  out_.clear();
  globals_.clear();
  intConsts_.clear();
  stringConsts_.clear();
  tmp_ = 0; label_ = 0;
  emitPrelude();
  for (auto& c : prog_.classes) emitClassMeta(*c);
  emitBuiltinMethods();
  for (auto& c : prog_.classes)
    for (auto& f : c->features)
      if (auto* m = dynamic_cast<const Method*>(f.get())) emitMethod(c->name, *m);
  emitMain();
  out_ += globals_;   // 全局常量统一放到模块级（LLVM 要求）
  return out_;
}

} // namespace cool
