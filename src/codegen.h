#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "ast.h"

namespace cool {

struct AttrSlot {
  std::string name;
  std::string type;
  const Expr* init;   // 可为 nullptr
};

struct MethodRef {
  std::string funcName;   // LLVM 函数名，如 "@C.m" / "@Object.abort"
  int arity = 0;          // 形参个数
};

class CodeGen : public ExprVisitor {
public:
  explicit CodeGen(const Program& prog) : prog_(prog) { buildClassInfo(); }
  std::string generate();

private:
  const Program& prog_;
  std::string out_;
  std::string globals_;   // 延迟 emit 的全局常量（整数字面量/字符串字面量）

  // 类信息
  std::unordered_map<std::string, const ClassDef*> classMap_;
  std::unordered_map<std::string, std::string> parentMap_;
  std::unordered_map<std::string, int> classId_;                       // 类 -> 整数 ID
  std::unordered_map<std::string, std::vector<std::string>> descendants_; // 类 -> 自身+所有后代
  std::unordered_map<std::string, int> methodIndex_;                   // 方法名 -> vtable 下标
  std::unordered_map<std::string, std::unordered_map<std::string, MethodRef>> methods_; // 类 -> (方法 -> ref)
  std::unordered_map<std::string, std::vector<AttrSlot>> attrs_;       // 类 -> 有序属性槽
  int nextMethodIndex_ = 0;

  // 生成状态
  std::string result_;
  int tmp_ = 0;
  int label_ = 0;
  std::string currentClass_;
  std::string currentBlock_;   // 当前正在填充的基本块标签（phi 前驱用）
  std::vector<std::unordered_map<std::string, std::string>> scopes_; // 变量名 -> alloca 名
  std::unordered_map<int, std::string> intConsts_;
  std::vector<std::string> stringConsts_;
  std::unordered_map<std::string, std::string> msgConsts_;   // 运行时错误信息 -> gep 表达式

  // 工具
  void buildClassInfo();
  void emit(const std::string& s) { out_ += s; out_ += "\n"; }
  std::string fresh() { return "%t" + std::to_string(tmp_++); }
  std::string freshLabel() { return "bb" + std::to_string(label_++); }
  // 开一个基本块，并记住"当前所在的块"。
  // phi 的前驱必须是控制流真正流出的那个块，而子表达式（if/while/case）会自己开新块，
  // 所以不能用"进入分支时的标签"，必须用生成完之后的 currentBlock_。
  void emitLabel(const std::string& l) { emit(l + ":"); currentBlock_ = l; }
  // 当前 self 所在的槽。不能写死 "%self"：属性初始化式是在 allocateAndInit 里内联展开的，
  // 那里 self 绑定到一个新建的 alloca（指向正在构造的对象），而 "%self" 是**外层方法**的 self。
  // 写死会让属性初始化式读到错误的对象（@main 里更是根本没有 %self）。
  std::string selfSlot() const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto f = it->find("self");
      if (f != it->end()) return f->second;
    }
    return "%self";
  }
  std::string sizeOf(const std::string& cls);
  int attrOffset(const std::string& cls, const std::string& name);
  std::string funcType(int arity) const;   // "i8* (i8*, ...)*"

  std::string intConst(int v);
  std::string strConst(const std::string& lexeme);
  std::string defaultFor(const std::string& type);
  std::string unboxInt(const std::string& v);
  std::string unboxBool(const std::string& v);
  std::string unboxStrChars(const std::string& v);
  std::string boxInt(const std::string& i32v);
  std::string boxBool(const std::string& i32v);
  std::string wrapStr(const std::string& chars);
  std::string codegenExpr(const Expr& e);
  std::string allocateAndInit(const std::string& cls);   // new 对象的分配+属性初始化

  // 运行时错误（手册 §13 列了 6 种）。msgConst 发一个私有字符串常量，
  // emitGuard 在条件成立时跳到一个报错并 unreachable 的块。
  // 注意 emitGuard 会切分基本块，所以它必须走 emitLabel 维护 currentBlock_，
  // 否则后面 if/case 的 phi 前驱会写成一个已经不是"流出块"的标签。
  std::string msgConst(const std::string& msg);
  void emitGuard(const std::string& i1cond, const std::string& msg);
  void guardVoid(const std::string& v, const std::string& msg);   // v == null 则报错

  void emitPrelude();
  void emitClassMeta(const ClassDef& c);
  void emitMethod(const std::string& cls, const Method& m);
  void emitBuiltinMethods();
  void emitMain();

  void visit(const IntConst&) override;
  void visit(const StringConst&) override;
  void visit(const BoolConst&) override;
  void visit(const Object&) override;
  void visit(const Assign&) override;
  void visit(const Dispatch&) override;
  void visit(const StaticDispatch&) override;
  void visit(const If&) override;
  void visit(const While&) override;
  void visit(const Block&) override;
  void visit(const Let&) override;
  void visit(const Case&) override;
  void visit(const New&) override;
  void visit(const IsVoid&) override;
  void visit(const Binary&) override;
  void visit(const Neg&) override;
  void visit(const Not&) override;
  void visit(const NoExpr&) override;
};

} // namespace cool
