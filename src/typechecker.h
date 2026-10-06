#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "ast.h"
#include "symtab.h"

namespace cool {

struct MethodSig {
  std::vector<std::string> argTypes;
  std::string retType;
  bool operator==(const MethodSig& o) const {
    return argTypes == o.argTypes && retType == o.retType;
  }
};

// 类型检查器：实现 Cool 的完整类型规则（PA3 语义分析）
class TypeChecker : public ExprVisitor {
public:
  explicit TypeChecker(const Program& prog) : prog_(prog) {}
  bool check();   // 返回 true 表示无错误

private:
  const Program& prog_;

  // 类环境
  std::unordered_map<std::string, const ClassDef*> classMap_;
  std::unordered_map<std::string, std::string> parentMap_;
  std::unordered_map<std::string, std::unordered_map<std::string, MethodSig>> methodEnv_;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> attrEnv_;

  // 类型检查状态
  std::string currentClass_;
  SymbolTable O_;
  std::string result_;   // 最近一次 visit 得到的类型
  int errorCount_ = 0;

  // 环境构建
  void buildEnvironments();
  void installBuiltins();
  const std::unordered_map<std::string, MethodSig>& methodsOf(const std::string& cls);
  const std::unordered_map<std::string, std::string>& attrsOf(const std::string& cls);
  bool hasCycle(const std::string& cls, std::unordered_set<std::string>& visiting,
                std::unordered_set<std::string>& done);

  // 检查类
  void checkClass(const ClassDef& c);
  void checkFeature(const Feature& f);

  // 工具
  void error(int line, const std::string& msg);
  bool isType(const std::string& t) const;                     // 是否是已知类型/SELF_TYPE
  bool conforms(const std::string& a, const std::string& b);   // a <= b
  std::string lub(const std::string& a, const std::string& b); // 最小上界 ⊔
  std::string lca(const std::string& a, const std::string& b); // 最近公共祖先
  std::string resolveSelf(const std::string& t) const {       // SELF_TYPE_C
    return t == "SELF_TYPE" ? currentClass_ : t;
  }
  std::string typeOf(const Expr& e) {
    e.accept(*this);
    const_cast<Expr&>(e).type = result_;   // 标注类型，供 codegen 使用
    return result_;
  }

  // 表达式类型规则
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
