# 03 · 语法分析（上）：CFG、递归下降与 AST 构建

- 主题：compilers
- 状态：[精]
- 标签：#编译原理 #CS143 #语法分析 #上下文无关文法 #递归下降 #优先级 #AST
- 一句话结论：语法分析用**上下文无关文法**描述语言结构，用**递归下降 + 优先级爬升**把 Token 流变成**抽象语法树 AST**。
- 相关笔记：`01_Cool语言精讲.md`（文法来源）、`04_语法分析(下).md`（LR/LALR 自底向上）
- 产出代码：`projects/coolc/src/ast.h`、`parser.h`、`parser.cpp`、`dump.cpp`

---

## 精炼段

语法分析（parsing）是编译第二步：验证 Token 序列是否符合语言文法，并把它组织成**语法树**。理论支柱是**上下文无关文法（CFG）**：它用产生式描述"一个非终结符如何展开"。实际实现我们选**递归下降 + 优先级爬升**——这正是 Clang/Rust/Go/v8 等现代工业编译器的做法。Cool 的文法天生二义（`1+2*3` 有两种解释），我们用**优先级分层**消除它。产物是 **AST**：一棵丢弃了分号/括号等"噪音"、只保留语义本质的树，是后面语义分析（doc 05/06）和代码生成（doc 09）的操作对象。

---

## 1. 语法分析的任务

```
Token 流:  IF ( ID(x) GT(>) INT(3) ) THEN ...
                │  语法分析
                ▼
抽象语法树:        if
                 /  \
               >    <- ...
              / \
             x   3
```

**为什么要用 CFG 而不是正则？** 因为正则（doc 02）**表达不了嵌套结构**——它无法描述"括号必须配对"、`if...fi` 的匹配这类"递归嵌套"。而 CFG 可以（产生式可以递归）。**一句话**：正则描述 token（词的形状），CFG 描述程序（句子的结构）。

---

## 2. 上下文无关文法（CFG）基础

### 2.1 定义

一个 CFG 是四元组 `G = (N, T, P, S)`：
- **N**：非终结符（语法变量，如 `expr`、`class`）
- **T**：终结符（token，如 `IF`、`+`、`ID`）
- **P**：产生式（规则），形如 `A → α`（A 是非终结符，α 是终结符/非终结符串）
- **S**：开始符号

Cool 的完整文法就是 01 篇 §4 的 Figure 1。例如：

```
expr ::= expr + expr       -- 加法
       | ID                -- 变量
```

### 2.2 推导与语法树

- **推导**：从 S 出发，反复用产生式替换非终结符，直到全是终结符。
  - **最左推导**：每次替换最左边的非终结符（对应递归下降）。
- **语法树（parse tree）**：推导过程的树形记录。根 = S，叶 = 终结符。

### 2.3 二义性（Ambiguity）——文法的大敌

**定义**：若某个串有**多于一棵**语法树，文法就是二义的。

经典例子：文法 `E → E + E | E * E | id`，串 `id + id * id`：

```
  树A（先算 *）        树B（先算 +）
      +                   *
     / \                 / \
   id   *               +   id
       / \             / \
     id   id         id   id
```

树 A 表示 `id + (id*id)`，树 B 表示 `(id+id)*id`——语义完全不同。**Cool 的文法也是二义的**（`1+2*3` 就有歧义），所以我们必须**人为规定优先级和结合性**来消除二义（§4）。

> **消除二义的三板斧**：① 优先级（`*` 比 `+` 紧）② 结合性（左结合/右结合）③ 括号强制分组。这正是 01 篇 §5 那张优先级表的来源。

---

## 3. 自顶向下分析（Top-Down）与递归下降

### 3.1 左递归：递归下降的死敌

自顶向下分析"一边看输入一边展开产生式"。但如果文法有**左递归**（`E → E + T`），递归下降会**无限递归**（为了分析 E，先要分析 E……）。

**消除左递归**：把 `E → E + T | T` 改写成右递归：

```
E  → T E'
E' → + T E' | ε
```

### 3.2 预测分析：FIRST 与 FOLLOW（LL(1) 的核心）

递归下降要在多个产生式间**做选择**，靠"前瞻"。两个集合帮你判断：

- **FIRST(α)**：能从 α 推导出的**开头终结符**集合。
  - 例：`FIRST(if expr then ...) = { IF }`，`FIRST(ID) = { ID }`。
- **FOLLOW(A)**：在某些推导中，**紧跟在 A 后面**出现的终结符集合。

**LL(1) 文法**：若对任意 `A → α | β`，都有 `FIRST(α) ∩ FIRST(β) = ∅`（且若 ε∈FIRST(α) 则 `FIRST(β)∩FOLLOW(A)=∅`），则只需 **1 个 token 前瞻**即可无歧义地选产生式。

**直觉**：LL(1) = "看到下一个 token 就知道该走哪条路"。

---

## 4. 优先级爬升（Precedence Climbing）——把优先级编码进递归下降

Cool 的运算符太多、优先级分层复杂，与其消除左递归写成 LL(1)，不如用**优先级爬升**：为**每个优先级**写一层函数，层层下钻。

### 4.1 Cool 优先级 → 函数调用链（本文的核心设计）

回顾 01 篇 §5 的优先级（**由松到紧**）：

| 优先级（松→紧） | 运算符 | 处理函数 | 结合性 |
|---|---|---|---|
| 最松 | `<-` | `parseExpr` | 右结合 |
| | `not` | `parseNot` | 前缀 |
| | `<= < =` | `parseCompare` | 非结合 |
| | `+ -` | `parseAddSub` | 左结合 |
| | `* /` | `parseMulDiv` | 左结合 |
| | `isvoid` | `parseIsvoid` | 前缀 |
| | `~` | `parseNeg` | 前缀 |
| 最紧 | `. @`（分派） | `parseDispatch` | 后缀 |

```
parseExpr ──► parseNot ──► parseCompare ──► parseAddSub ──► parseMulDiv
   │                                                                  │
   ▼                                                                  ▼
parseIsvoid ──► parseNeg ──► parseDispatch ──► parsePrimary（原子）
```

**为什么这样排**：每层函数只处理自己那一个优先级的运算符，遇到更紧的运算符就"下钻"到下一层函数；下一层解析完再回来。于是 `1 + 2 * 3` 中，`parseAddSub` 解析 `1` 时下钻到 `parseMulDiv`，后者把 `2 * 3` 整个吃掉，`parseAddSub` 再拼 `1 + (2*3)`。

### 4.2 关键实现：左结合循环 + 右结合递归

**左结合**（`+ - * /`）用**循环**：

```cpp
std::unique_ptr<Expr> Parser::parseAddSub() {
  auto lhs = parseMulDiv();
  while (at(Tok::PLUS) || at(Tok::MINUS)) {   // 持续向右结合
    BinOp op = at(Tok::PLUS) ? BinOp::Plus : BinOp::Minus;
    advance();
    auto b = std::make_unique<Binary>();
    b->op = op;
    b->lhs = std::move(lhs);
    b->rhs = parseMulDiv();                   // 右操作数下钻到更紧层
    lhs = std::move(b);
  }
  return lhs;
}
```

**右结合**（赋值 `<-`）用**递归**：

```cpp
std::unique_ptr<Expr> Parser::parseExpr() {
  if (at(Tok::OBJECTID) && nxt_.kind == Tok::ASSIGN) {  // 2-token 前瞻
    Token id = cur_; advance(); advance();              // 吃掉 ID <-
    auto a = std::make_unique<Assign>();
    a->name = id.lexeme;
    a->rhs = parseExpr();                               // 递归 → 右结合
    return a;
  }
  return parseNot();
}
```

**前缀运算符**（`not ~ isvoid`）用**递归**：

```cpp
std::unique_ptr<Expr> Parser::parseNeg() {
  if (at(Tok::TILDE)) { advance();
    auto n = std::make_unique<Neg>();
    n->expr = parseNeg();                 // ~ ~x 右递归
    return n;
  }
  return parseDispatch();
}
```

---

## 5. 抽象语法树（AST）——语法分析的产物

**为什么不用"语法树（parse tree）"而用"抽象语法树（AST）"？**

parse tree 忠实记录**每个**产生式，含大量噪音（括号、分号、`then`/`fi` 关键字都是叶节点）。AST 则**丢弃语法噪音，只保留语义**：

```
源码:  (x + 3) * y
parse tree 有 10+ 个节点（含括号节点）   AST 只有 3 个节点:  *( +(x,3), y )
```

AST 的设计（`ast.h`）要点：
- **表达式**用一个基类 `Expr` + 派生类（`IntConst`、`Dispatch`、`If`、`Binary`……），每个节点带 `line`（行号，供报错）。
- 用 `std::unique_ptr` 表达**所有权**（孩子归父节点所有）。
- 用 **Visitor 模式**（`ExprVisitor` + 每个节点的 `accept`）——这样后面的**类型检查（doc 06）和代码生成（doc 09）都能复用同一套遍历框架**，不用在每个节点上硬编码逻辑。

> 这是真实的工业设计：Clang 的 AST 就是"节点类 + 访问者"。

---

## 6. 代码（本阶段的全部代码）

> 完整代码在 `projects/coolc/src/`。下面给**核心骨架**，完整文件请直接读源码（行号已对齐）。

### 6.1 `ast.h`（节选：节点与 Visitor）

```cpp
struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual void visit(const IntConst&) = 0;
  virtual void visit(const Dispatch&) = 0;
  virtual void visit(const If&) = 0;
  // ... 每个节点一个 visit
};

struct Expr {
  int line = 0;
  virtual ~Expr() = default;
  virtual void accept(ExprVisitor& v) const = 0;
};

struct Binary : Expr {                 // + - * / < <= =
  BinOp op;
  std::unique_ptr<Expr> lhs, rhs;
  void accept(ExprVisitor& v) const override { v.visit(*this); }
};
// ... 其余 18 种表达式节点同构
```

### 6.2 `parser.h`

```cpp
class Parser {
public:
  explicit Parser(std::string src) : lexer_(std::move(src)) {
    cur_ = lexer_.next(); nxt_ = lexer_.next();   // 2-token 前瞻
  }
  Program parseProgram();
private:
  Lexer lexer_;
  Token cur_, nxt_;                 // 当前 / 下一个 token
  void advance() { cur_ = nxt_; nxt_ = lexer_.next(); }

  std::unique_ptr<Expr> parseExpr();      // <-
  std::unique_ptr<Expr> parseNot();       // not
  std::unique_ptr<Expr> parseCompare();   // <= < =
  std::unique_ptr<Expr> parseAddSub();    // + -
  std::unique_ptr<Expr> parseMulDiv();    // * /
  std::unique_ptr<Expr> parseIsvoid();    // isvoid
  std::unique_ptr<Expr> parseNeg();       // ~
  std::unique_ptr<Expr> parseDispatch();  // . @
  std::unique_ptr<Expr> parsePrimary();   // 原子
};
```

### 6.3 `parser.cpp` 关键逻辑

**分派（最紧，后缀）**——`e.f(args)` / `e@T.f(args)`：

```cpp
std::unique_ptr<Expr> Parser::parseDispatch() {
  auto e = parsePrimary();
  for (;;) {
    if (at(Tok::DOT)) {                       // 动态分派
      advance();
      std::string m = expect(Tok::OBJECTID, "method name").lexeme;
      auto d = std::make_unique<Dispatch>();
      d->receiver = std::move(e);
      d->method = m;
      d->args = parseDispatchArgs();          // 解析 ( e1, e2, ... )
      e = std::move(d);
    } else if (at(Tok::AT)) {                 // 静态分派 e@T.f(...)
      advance();
      std::string ty = expect(Tok::TYPEID, "type").lexeme;
      expect(Tok::DOT, "'.'");
      std::string m = expect(Tok::OBJECTID, "method").lexeme;
      auto d = std::make_unique<StaticDispatch>();
      d->receiver = std::move(e); d->type = ty; d->method = m;
      d->args = parseDispatchArgs();
      e = std::move(d);
    } else break;
  }
  return e;
}
```

**原子（primary）**——含 self-dispatch 与各类复合表达式：

```cpp
std::unique_ptr<Expr> Parser::parsePrimary() {
  Token t = cur_;
  switch (cur_.kind) {
    case Tok::OBJECTID: {
      advance();
      if (at(Tok::LPAREN)) {                  // self-dispatch: f(args) == self.f(args)
        advance();
        auto d = std::make_unique<Dispatch>();
        auto self = std::make_unique<Object>();
        self->name = "self";
        d->receiver = std::move(self);
        d->method = t.lexeme;
        if (!at(Tok::RPAREN)) { /* 解析实参 */ }
        expect(Tok::RPAREN, "')'");
        return d;
      }
      auto o = std::make_unique<Object>();
      o->name = t.lexeme;                      // 普通变量引用
      return o;
    }
    case Tok::INT_CONST: { /* ... IntConst ... */ }
    case Tok::STR_CONST: { /* ... StringConst ... */ }
    case Tok::LPAREN:  { advance(); auto e = parseExpr(); expect(Tok::RPAREN,"')'"); return e; }
    case Tok::IF:    return parseIf();
    case Tok::WHILE: return parseWhile();
    case Tok::LBRACE: return parseBlock();
    case Tok::LET:   return parseLet();
    case Tok::CASE:  return parseCase();
    case Tok::NEW:   { /* ... New ... */ }
    default: error("unexpected token '" + cur_.lexeme + "'");
  }
}
```

**关键设计点**（为什么这样写）：
1. **self-dispatch**（`f(args)`）被翻译成 `Dispatch(Object("self"), "f", args)`——因为 Cool 里 `self` 是隐式绑定的，这样统一了后续处理。
2. **`(expr)`** 里调 `parseExpr()`（而非更紧的层），因为括号要"重置优先级"。
3. **赋值需要 2-token 前瞻**：`ID` 后面跟 `<-` 才是赋值，跟别的就是变量引用。

### 6.4 `dump.cpp`：用 Visitor 打印 AST

`Dumper` 实现了 `ExprVisitor`，把每个节点打印成缩进树（§7 的输出就是它）。

---

## 7. 测试结果（CP2 达成）

对 `tests/factorial.cl`：

```
program
  class Main inherits IO
    method fact(n : Int) : Int
      if
        binary =            ← n = 0（= 是相等比较）
          object n
          int 0
        int 1
        binary *            ← n * fact(n - 1)（* 的右操作数是分派）
          object n
          dispatch .fact    ← self-dispatch fact(n-1)
            object self
            binary -
              object n
              int 1
    method main() : Object
      block
        dispatch .out_int
          object self
          dispatch .fact
            object self
            int 5
```

**验证点**（对照 01 篇的优先级表）：
- `n * fact(n - 1)` 中 `fact(n-1)` 作为 `*` 的右操作数整块解析 ✅（分派比 `*` 紧）
- `n - 1` 正确成为 `fact` 的实参 ✅
- `fact(n-1)` 是 self-dispatch → `dispatch .fact (object self, ...)` ✅
- `if n = 0 then...` 中 `=` 是相等比较（不是赋值）✅

---

## 8. 递归下降 vs LR 自底向上（预告）

| | 递归下降（本文） | LR/LALR（doc 04） |
|---|---|---|
| 方向 | 自顶向下 | 自底向上 |
| 文法要求 | 无左递归、需消除二义 | 更广，左递归 OK |
| 实现 | 手写，直观可读 | 表驱动（yacc/bison 生成） |
| 工业代表 | Clang、Rust、Go、v8 | gcc（早期）、yacc 系 |
| 错误信息 | 好（可定制） | 差（"unexpected token"） |

我们选递归下降（工业主流），但 CS143 原课 PA2 用 bison（LALR），doc 04 会讲透 LR 理论并给出 bison 参考实现。

---

## 9. 本文你掌握了什么 / 易错点 / 下一步

**掌握**：CFG 四元组、推导与语法树、二义性、左递归、FIRST/FOLLOW、LL(1)、优先级爬升、AST 设计、递归下降 parser 完整实现。

**易错点**：
1. **`=` 是相等（Binary/Eq），`<-` 才是赋值（Assign）**——parser 里它们走完全不同的路径。
2. 赋值 `<-` 是**右结合**（用递归），`+ - * /` 是**左结合**（用循环）。
3. 比较运算符 `<= < =` **非结合**：`a < b < c` 是语法错误。
4. self-dispatch `f(args)` 要合成 receiver=`self`，别和变量引用 `f` 混淆。
5. `(expr)` 里要调 `parseExpr()` 重置优先级，否则 `(1+2)*3` 会解析错。

**下一步**：读 `04_语法分析(下).md`，学自底向上的 LR(0)/SLR/LR(1)/LALR 和 bison，补齐 CS143 对 parser 理论的全部要求。
