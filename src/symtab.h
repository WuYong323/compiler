#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace cool {

// 作用域栈：维护"变量名 → 类型"的嵌套作用域（词法作用域）
class SymbolTable {
public:
  void pushScope() { scopes_.emplace_back(); }
  void popScope() { scopes_.pop_back(); }

  // 在当前（最内层）作用域声明一个名字
  void add(const std::string& name, const std::string& type) {
    scopes_.back()[name] = type;
  }

  // 从最内层往外查找；未找到返回 nullopt
  std::optional<std::string> lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) return found->second;
    }
    return std::nullopt;
  }

  bool empty() const { return scopes_.empty(); }

private:
  std::vector<std::unordered_map<std::string, std::string>> scopes_;
};

} // namespace cool
