#pragma once
#include <ostream>
#include "ast.h"

namespace cool {
// 打印 AST 的结构树（CP2 调试用）
void dumpProgram(const Program& p, std::ostream& os);
}
