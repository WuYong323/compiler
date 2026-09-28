# 03c · AST 与 parser 实现：为什么要 Visitor，以及 primary 层的全部决策

- 主题：compilers
- 状态：[精]
- 标签：#编译原理 #CS143 #AST #Visitor模式 #表达式问题 #递归下降 #CP2
- 一句话结论：AST 不是"简化版语法树"，它是**为后续阶段设计的数据结构**——丢掉 `fi`/`;`/括号是因为它们的信息已经体现在树形里，保留 `line` 是因为报错需要它；`ast.h` 里 18 个 `accept` 不是样板代码癖，它解决的是**"18 种节点 × 3 个遍历者"的组合爆炸**，而同一份代码里的 `Feature` 只有 2 种，所以它就没用 Visitor——这个对照本身就是设计权衡的答案。
- 相关笔记：`03b_从文法到递归下降.md`（函数链与优先级）、`05a_符号表与作用域.md`（下一站：在这棵树上跑符号表）、`06b_类型规则的实现.md`（第一个 Visitor 实现者，也是 `Expr::type` 的写入者）
- 产出代码：`coolc/src/ast.h`、`coolc/src/parser.cpp`（primary 层与复合结构）、`coolc/src/dump.cpp`
- 里程碑：**CP2 达成** —— `coolc.exe tests/factorial.cl --dump` 打印正确 AST

---

## 精炼段

`03a` 解决了"文法怎么改才能被递归下降处理"，`03b` 解决了"优先级怎么编码进函数链"。本篇解决最后一块：**产物长什么样**。核心有三个判断。第一，AST 该丢什么该留什么不是审美问题，判据是"后续阶段还需不需要它"——`fi` 的信息已经在树形里（丢），`line` 在 `06`/`09` 报错时要用（留），括号影响了树形之后就没用了（丢，所以 `ast.h` 里没有 `Paren` 节点）。第二，Visitor 模式的必要性可以精确量化：18 种节点 × 3 个遍历者（dump/typecheck/codegen），不用 Visitor 就是 54 处散落的 `dynamic_cast`，加一个遍历者要改 18 个文件。同一份 `ast.h` 里 `Feature` 只有 2 种，所以它**故意没用** Visitor 而是用 `dynamic_cast`——这个真实的不一致恰好是"什么时候该上 Visitor"的答案。第三，`parsePrimary` 的 11 条产生式里藏着 `03b` §2.4 那个 LL(1) 冲突的第二处修法（左因子分解），以及 self-dispatch 为什么要**合成**一个 `Object("self")` 节点。最后 §6 跑通 CP2，§7 交代我实测发现的两个真实缺陷。

---

## 0. 本篇要回答的六个问题

1. parse tree 和 AST 具体差在哪？给一个"丢了但没丢失信息"的例子，和一个"必须留"的例子。
2. `ast.h` 里为什么**没有** `Paren` 节点？括号的信息去哪了？
3. Visitor 模式解决的到底是什么问题？不用它，代价能算出来吗？
4. 同一份 `ast.h` 里，`Expr` 用 Visitor 而 `Feature` 用 `dynamic_cast`。这是疏忽还是有道理？
5. `parsePrimary` 看到一个 `ID`，怎么区分"变量引用"和"self 分派"？这和 `03b` 的 `nxt_` 是同一招吗？
6. self-dispatch `f(3)` 为什么要**造**一个 `Object("self")` 塞进树，而不是留空？

---

## 1. AST vs parse tree：判据是"后续阶段还要不要"

### 1.1 先看差距有多大

拿 `(x + 3) * y` 说话。按 `03b` §1 那份分层文法，**完整的 parse tree**（每一层非终结符都是一个节点）：

```
E0
└─ E1
   └─ E2
      └─ E3
         └─ E4 ──────────────┬──────────┬─────────────┐
            │                │          │             │
            E5               '*'        E5            │
            └─ E6                       └─ E6         │
               └─ E7                       └─ E7      │
                  └─ E8                       └─ E8   │
                     ├─ '('                      └─ ID(y)
                     ├─ E0
                     │  └─ …6层… └─ E8 → ID(x)
                     │     '+'
                     │     …6层… └─ E8 → INT(3)
                     └─ ')'
```

三十多个节点，其中绝大多数是 `E5 → E6 → E7 → E8` 这种**只有一个孩子的"穿透节点"**（`03b` §5.3 要点 ④ 的那 8 层空穿透，在树上就是这些）。

**AST**：

```
Binary(*)
├─ Binary(+)
│  ├─ Object(x)
│  └─ IntConst(3)
└─ Object(y)
```

5 个节点。

### 1.2 丢掉的三类东西，以及为什么丢得掉

| 丢掉的 | 例子 | 为什么信息没丢失 |
|---|---|---|
| **穿透节点** | `E5 → E6 → E7 → E8` | 它们只表达"优先级层级"，而层级的结果**已经体现为树的形状**。留着是纯冗余。 |
| **分组符号** | `(` `)` | 括号的作用是**影响树形**。树一旦建成（`+` 在 `*` 的孩子位），括号的使命完成。`03b` §3.3 讲过，所以 `parsePrimary` 的 `LPAREN` 分支直接 `return e`，不包节点。 |
| **结构标记** | `fi` `pool` `esac` `;` `then` `else` | 它们是**给 parser 认路用的**。`If` 节点有 `cond`/`then_`/`else_` 三个字段，三者的边界已经明确，`then`/`else`/`fi` 不再承载任何信息。 |

**统一判据（这条比"丢掉噪音"这种说法好用）**：

> 一个语法元素该不该进 AST，看**后续阶段（`05` 语义、`06` 类型、`09` codegen）会不会读它**。
>
> 读 → 留；不读 → 丢。

### 1.3 必须留下的：`line`

`ast.h:40`：

```cpp
struct Expr {
  int line = 0;
  std::string type;   // 类型检查后填入的静态类型（codegen 使用）
  virtual ~Expr() = default;
  virtual void accept(ExprVisitor& v) const = 0;
};
```

**`line` 为什么必须留**：源码文本在 parse 之后就被丢掉了，但 `06` 类型检查要报 `line 7: type Int does not conform to String`。没有 `line`，编译器只能说"你的程序有类型错误"——这种编译器没人用。

**`type` 字段更有意思**：parser **不填**它（parser 不懂类型），`06` 的 typechecker 填，`09` 的 codegen 读。

> **这是 AST 的第二重身份**：它不只是 parser 的输出，还是**贯穿各阶段的共享数据结构**，每个阶段往上"挂"自己的标注。编译器术语叫**装饰语法树**（decorated / annotated AST）。
>
> 所以 §1.2 的判据要再补一句：**AST 的字段不只服务于"表达源码"，也服务于"承载后续分析结果"。** `type` 就是一个在 parse 阶段完全无用、但必须预留的字段。

**运算符的 `line` 取谁的？** 看 `parseAddSub`（`parser.cpp:128`）：

```cpp
BinOp op = at(Tok::PLUS) ? BinOp::Plus : BinOp::Minus;
Token t = cur_; advance();
auto b = std::make_unique<Binary>();
b->line = t.line;        // ★ 运算符自己的行号，不是 lhs 的
```

为什么？考虑跨行表达式：

```cool
someVeryLongExpression
  + anotherThing        -- 类型错误发生在这个 '+' 上
```

报错应该指向第 2 行（`+` 所在行），而不是第 1 行。**报错位置要落在"出问题的那个操作"上**，所以取运算符的行号。

> `03b` §4.1 的自查清单里那条"`Token t = cur_;` 必须在 `advance()` 之前"，原因就在这：`advance()` 之后 `cur_` 已经是下一个 token，行号就取错了。这是手写 parser 最高频的低级错误。

### 1.4 什么时候你反而需要 parse tree

| 工具类型 | 要哪种树 | 原因 |
|---|---|---|
| 编译器（我们） | **AST** | 只需要语义，输出是机器码，不用还原源码 |
| 格式化工具（clang-format、black） | **CST**（保留括号/空白/注释） | 要输出源码文本，每个字符都得还原 |
| IDE 重构、linter | **CST** | 要做"源码→源码"变换，且要保留用户的格式 |
| 增量编译 / LSP | **CST + 容错** | 代码编辑中途是语法错误的，仍要建出部分树 |

CST 的工业实现：Rust 的 `rowan`（rust-analyzer 用）、Roslyn 的 red-green tree、tree-sitter。

> 取舍点一句话：**要不要从树还原出原始文本**。要 → CST；不要 → AST。
>
> 一个推论：我们的 parser 对 `(1+2)` 和 `1+2` 建出**完全相同**的树。对编译器这是优点（少一层间接），对格式化工具这是灾难（用户写的括号没了）。

---

## 2. Visitor 模式：把代价算出来

上一版 03 笔记说 Visitor 是"真实的工业设计，Clang 就是这样"。这是**结论**不是**理由**。这节把理由算清楚。

### 2.1 问题的形状

`ast.h` 有 **18 种**表达式节点：

```
IntConst  StringConst  BoolConst  Object  Assign  Dispatch  StaticDispatch
If  While  Block  Let  Case  New  IsVoid  Binary  Neg  Not  NoExpr
```

要在这棵树上跑 **3 个**遍历者：

| 遍历者 | 在哪篇 | 干什么 |
|---|---|---|
| `Dumper` | `03c`（本篇） | 打印结构树 |
| `TypeChecker` | `06` | 算每个节点的类型，填 `type` 字段 |
| `CodeGen` | `09` | 生成 LLVM IR |

**18 × 3 = 54 个"节点 × 操作"的组合。** 问题是：这 54 份代码该怎么组织？

### 2.2 方案 A：节点上加虚函数（面向对象的直觉做法）

```cpp
struct Expr {
  virtual void dump(std::ostream&) const = 0;
  virtual std::string typecheck(TypeEnv&) const = 0;
  virtual std::string codegen(IRBuilder&) const = 0;
};
```

每个节点实现这三个方法。**代价**：

- 加第 4 个遍历者（比如 `10` 篇的常量折叠）→ 要在 `Expr` 加一个纯虚函数 → **18 个类全部得改**；
- `ast.h` 被迫 `#include` 类型检查、codegen 的头文件 → 前端所有模块耦合成一团；
- 一个 `If` 节点的类型检查逻辑和 codegen 逻辑挤在同一个类里，而它们本该各自内聚。

### 2.3 方案 B：`dynamic_cast` 链

```cpp
void dump(const Expr& e) {
  if (auto* p = dynamic_cast<const IntConst*>(&e))      { … }
  else if (auto* p = dynamic_cast<const Binary*>(&e))   { … }
  else if (auto* p = dynamic_cast<const If*>(&e))       { … }
  // … 18 个分支
}
```

**代价**：

- **漏掉一种节点，编译器不报错**，运行时静默走到 `else`（或者什么都不做）。这是最危险的一点；
- 18 个 `dynamic_cast` 顺序比较，比虚函数一次跳转慢；
- 新增节点类型时，要**手工检查所有** `dynamic_cast` 链有没有漏。

### 2.4 方案 C：Visitor（项目采用）

```cpp
struct ExprVisitor {
  virtual void visit(const IntConst&) = 0;
  virtual void visit(const Binary&) = 0;
  // … 18 个纯虚函数
};

struct Expr {
  virtual void accept(ExprVisitor& v) const = 0;
};

struct Binary : Expr {
  BinOp op; std::unique_ptr<Expr> lhs, rhs;
  void accept(ExprVisitor& v) const override { v.visit(*this); }   // ★ 这一行
};
```

**`accept` 那一行在干什么？** 它把"我是什么类型"的知识，从**调用方**转移到了**节点自己**：

```
调用方只知道 Expr&  →  e.accept(v)  →  虚函数跳转到 Binary::accept
                                     →  v.visit(*this)，此时 this 的静态类型已是 Binary&
                                     →  重载决议选中 visit(const Binary&)
```

**两次分派**：第一次靠虚函数（确定节点类型），第二次靠函数重载（选对 `visit`）。这就是**双分派**（double dispatch）——C++ 只有单分派（虚函数按 `this` 的动态类型），Visitor 用两跳模拟出双分派。

**收益（对比方案 A/B）**：

| | 加一个遍历者 | 加一个节点类型 | 漏写会怎样 |
|---|---|---|---|
| 方案 A（虚函数） | 改 18 个类 ✗ | 只写新类 ✓ | 编译错误 ✓ |
| 方案 B（dynamic_cast） | 新写一个函数 ✓ | 手工查所有链 ✗ | **静默错误** ✗✗ |
| 方案 C（Visitor） | **新写一个 visitor 类 ✓** | 改 visitor 接口 + 所有实现 ✗ | **编译错误** ✓ |

**关键一行**：方案 C 加遍历者时**一个现有文件都不用改**——`06` 的 `TypeChecker : public ExprVisitor`、`09` 的 `CodeGen : public ExprVisitor` 各自独立成文件。而且漏写一个 `visit` 是**编译错误**（纯虚函数没实现，类无法实例化），不会静默出错。

### 2.5 表达式问题：Visitor 不是免费的

注意上表方案 C 的"加节点类型"列是 ✗：给 Cool 加一个新表达式（比如 `for` 循环），要改 `ExprVisitor` 接口，然后 `Dumper`、`TypeChecker`、`CodeGen` **三个实现都得补一个 `visit`**。

这不是 Visitor 设计得不好，是一个**有名字的理论困难**：

> **表达式问题（Expression Problem，Wadler 1998）**：在静态类型语言里，无法同时做到"方便加新数据类型"和"方便加新操作"而不改已有代码。
>
> - 面向对象（方案 A）：加**类型**容易（新写一个类），加**操作**难（改所有类）；
> - 函数式 / Visitor（方案 C）：加**操作**容易（新写一个 visitor），加**类型**难（改所有 visitor）。

**那为什么编译器普遍选 Visitor？** 因为两个维度的变化频率天差地别：

```
语言的语法节点种类：  设计定稿后基本不动   （Cool 的 18 种是固定的，写完就不加了）
编译器的 pass 数量：  一直增长             （dump → typecheck → codegen → 优化 → lint → …）
```

**你要优化的是高频变化的那一维。** pass 会一直加，节点种类不会——所以选 Visitor。

> 这条推理方式本身值得记住：**看两个变化维度的频率，然后让高频那维变便宜。** 它在软件设计里到处适用，不只编译器。

### 2.6 ★ 同一份代码里的反例：`Feature` 为什么没用 Visitor

`ast.h:131`：

```cpp
struct Feature {
  int line = 0;
  std::string name;
  std::string type;
  virtual ~Feature() = default;
  // ★ 没有 accept
};
struct Attribute : Feature { std::unique_ptr<Expr> init; };
struct Method : Feature {
  std::vector<Formal> formals; std::unique_ptr<Expr> body;
};
```

没有 `accept`，没有 `FeatureVisitor`。所以 `dump.cpp:98` 只能用 `dynamic_cast`：

```cpp
void dumpFeature(const Feature& f, std::ostream& os, int indent) {
  if (auto* m = dynamic_cast<const Method*>(&f)) {
    …
  } else if (auto* a = dynamic_cast<const Attribute*>(&f)) {
    …
  }
}
```

**同一个文件里两套分派机制。这是疏忽吗？不是，是对的。** 算一下：

| | 类型数 | 遍历者数 | 组合 | 用 Visitor 的收益 |
|---|---|---|---|---|
| `Expr` | **18** | 3 | 54 | 大：省掉 54 处 `dynamic_cast`，漏写变编译错误 |
| `Feature` | **2** | 3 | 6 | 小：`if/else` 两个分支一眼看完，加一个 `FeatureVisitor` 接口反而是负担 |

**判据**：`类型数 × 遍历者数` 大到"漏写一个分支会出事"的程度，才值得上 Visitor。2 种类型的 `if/else`，漏写一眼就看出来。

> **所以 §0 问题 4 的答案是"有道理"。** 而且这个对照比任何教科书讲解都直接——同一个项目里，一边用一边不用，边界就是设计权衡本身。
>
> **给你一个自查**：如果以后 Cool 要加 `Feature` 的第三、第四种（比如加 `Constructor`、`Property`），什么时候该切到 Visitor？我的判断是 **4 种以上**，因为那时 3 个遍历者 × 4 种 = 12 处 `dynamic_cast`，开始容易漏。

### 2.7 遍历的递归结构

`Dumper` 里这个小工具（`dump.cpp:24`）值得单独看：

```cpp
class Dumper : public ExprVisitor {
  std::ostream& os_;
  int indent_ = 0;
  void pad() const { for (int i = 0; i < indent_; ++i) os_ << "  "; }
  void child(const Expr& e) { ++indent_; e.accept(*this); --indent_; }   // ★
```

`child()` 把"缩进 + 递归 + 恢复缩进"打成一个原语。于是每个 `visit` 都极简：

```cpp
void visit(const Binary& e) override {
  pad(); os_ << "binary " << binOpName(e.op) << "\n";
  child(*e.lhs); child(*e.rhs);            // 递归两个孩子
}
void visit(const If& e) override {
  pad(); os_ << "if\n";
  child(*e.cond); child(*e.then_); child(*e.else_);    // 三个孩子
}
```

**注意遍历顺序是"先访问自己，再访问孩子"——前序（preorder）。** 因为打印结构树要先打父节点。

**`09` codegen 会用后序**（先算孩子的值，再用它们算自己）。同一棵树、同一套 Visitor 框架，不同的 pass 用不同的遍历顺序：

| pass | 顺序 | 为什么 |
|---|---|---|
| `Dumper`（本篇） | 前序 | 父节点的缩进要先输出 |
| `TypeChecker`（`06`） | 后序 | 要先知道孩子的类型，才能算自己的 |
| `CodeGen`（`09`） | 后序 | 要先有孩子的值（寄存器/临时变量），才能生成自己的指令 |

> 这就是 `03b` §3.4 层级契约那个推论"深度 = 求值顺序"落地的地方：codegen 后序遍历，**深的先生成指令 = 先算**。优先级从树形转化成了指令顺序。

### 2.8 所有权：`unique_ptr` 表达什么

```cpp
struct Binary : Expr {
  BinOp op;
  std::unique_ptr<Expr> lhs, rhs;   // 孩子归父节点独占所有
};
```

**为什么是 `unique_ptr` 而不是 `shared_ptr` 或裸指针**：

- AST 是**树**不是图——每个节点恰好一个父亲，独占所有权语义精确匹配；
- 父节点析构 → 孩子自动递归析构 → 整棵树一次性释放，不用手写析构；
- `shared_ptr` 会多一个原子引用计数，而且允许"共享子树"这种我们**不想要**的东西（共享子树会让 `type` 字段标注产生歧义：同一个节点在两处可能该有不同类型）。

代价是 parser 里到处是 `std::move`：

```cpp
b->lhs = std::move(lhs);      // 所有权从局部变量转移到父节点
lhs = std::move(b);           // 再转移到累加器
```

**每个 `std::move` 都是一次所有权转移**。`03a` §4.6 那个"旧树进左孩子"的动作，在 C++ 里的物化就是这行 `std::move`。

> **和你 CUDA 经验的对照**：这套东西和你在 W4 做的 `unique_ptr` 管显存缓冲是同一个心智模型——**谁持有、谁释放、转移而非复制**。区别只是那边管的是 `cudaMalloc` 的指针，这边管的是 AST 节点。

---

## 3. `parsePrimary`：11 条产生式的分派

这是 `03b` §1 文法的 `E8` 层，也是整个 parser 唯一一个"多路分支"的地方。

### 3.1 骨架：用 `switch` 按 FIRST 集分派

`03b` §2.3 算出：

```
FIRST(E8) = { ID, INT_CONST, STR_CONST, TRUE, FALSE, '(', IF, WHILE, '{', LET, CASE, NEW }
```

12 个终结符，**除了 `ID` 那两条，其余全部互不相同** → 一个 `switch` 就能分派：

```cpp
std::unique_ptr<Expr> Parser::parsePrimary() {
  Token t = cur_;                      // ★ 先存，因为马上要 advance()
  switch (cur_.kind) {
    case Tok::OBJECTID:   { /* §3.2：两条产生式，要左因子分解 */ }
    case Tok::INT_CONST:  { advance(); /* 建 IntConst */ }
    case Tok::STR_CONST:  { advance(); /* 建 StringConst */ }
    case Tok::TRUE:       { advance(); /* 建 BoolConst(true) */ }
    case Tok::FALSE:      { advance(); /* 建 BoolConst(false) */ }
    case Tok::LPAREN:     { advance(); auto e = parseExpr();
                            expect(Tok::RPAREN, "')'"); return e; }   // 不建节点
    case Tok::IF:         return parseIf();
    case Tok::WHILE:      return parseWhile();
    case Tok::LBRACE:     return parseBlock();
    case Tok::LET:        return parseLet();
    case Tok::CASE:       return parseCase();
    case Tok::NEW:        { advance(); /* 建 New */ }
    default:              error("unexpected token '" + cur_.lexeme + "'");
  }
}
```

**`default` 分支是 parser 的唯一兜底**。`03b` §3.5 那个 `isvoid isvoid x` 报的 `unexpected token 'isvoid'`，就是从这里抛出来的——因为 `isvoid` 不在 `FIRST(E8)` 里。

### 3.2 `ID` 分支：左因子分解的实战

`E8` 有两条以 `ID` 开头的产生式（`03b` §2.4 认定的第二处 LL(1) 冲突）：

```
E8 → ID                      -- 变量引用
   | ID '(' Args ')'         -- self 分派
```

**修法是左因子分解**：先无条件吃掉 `ID`，再看下一个是不是 `(`。

```cpp
case Tok::OBJECTID: {
  advance();                          // ★ 无条件消耗 ID（公共前缀）
  if (at(Tok::LPAREN)) {              // ★ 现在才决策
    advance();
    auto d = std::make_unique<Dispatch>();
    d->line = t.line;
    auto self = std::make_unique<Object>();      // ★ §3.3：合成 self
    self->name = "self"; self->line = t.line;
    d->receiver = std::move(self);
    d->method = t.lexeme;             // ← 用之前存的 t，不是 cur_
    if (!at(Tok::RPAREN)) {
      for (;;) {
        d->args.push_back(parseExpr());
        if (!at(Tok::COMMA)) break;
        advance();
      }
    }
    expect(Tok::RPAREN, "')'");
    return d;
  }
  auto o = std::make_unique<Object>();
  o->name = t.lexeme; o->line = t.line;
  return o;                           // 普通变量引用
}
```

**为什么这里能左因子分解，而 `03b` §2.4 的 `parseExpr` 不能？**

两条分支**都留在 `parsePrimary` 内部**，吃掉 `ID` 后本地就能处理完。而 `parseExpr` 的两条分支一条留在本层（赋值），一条要交给下面 7 层函数（非赋值）——吃掉的 `ID` 得当参数穿过 7 层传下去，签名全得改。

> **两处同源冲突，两种不同修法，判据是"分支的归属层级"**：
>
> | 冲突 | 分支去向 | 修法 |
> |---|---|---|
> | `E0`：赋值 vs 其余 | 一条本层，一条下钻 7 层 | 加 `nxt_` 前瞻（`03b` §2.4） |
> | `E8`：变量 vs self 分派 | 两条都在本层 | 左因子分解（本节） |
>
> 这个对照比"遇到 FIRST 冲突就左因子分解"这种口诀有用——**修法取决于公共前缀吃掉之后，控制权还要不要往下传**。

### 3.3 self-dispatch：为什么要"造"一个 `self` 节点

Cool 规定 `f(3)` 等价于 `self.f(3)`（`01` 篇 §4.1）。代码里**合成**了一个 `Object("self")` 当接收者。

**为什么不留空（`receiver = nullptr`）**？

因为下游要处理两种情况了：

```cpp
// 如果 receiver 可以为空，06 和 09 都得这么写：
if (d.receiver) { /* 处理显式接收者 */ }
else            { /* 处理隐式 self */ }
```

`06` 类型检查要判断方法查找的起点类型，`09` codegen 要把接收者装进调用约定的第一个参数——**两个阶段各写一次分支，一共两处。** 而且以后加 pass 还要再写。

**合成 `self` 之后，`Dispatch` 节点永远有非空 receiver**，下游统一按一种情况处理。

> **这是编译器里极常见的一招，叫"语法糖消除"（desugaring）**：在 AST 构建期把"表面语法的便利写法"归一化成"内部统一形式"，让后续阶段少处理一种情况。
>
> 其他例子：
> - Python 的 `a < b < c` 在 AST 里被展开成 `Compare` 的多操作数形式；
> - Rust 的 `for x in it {}` 被 desugar 成 `loop { match it.next() {…} }`；
> - **你熟悉的例子**：`torch.nn.Linear` 的 `bias=True` 在 `torch.compile` 的 FX graph 里会被展开成显式的 `addmm` 节点，而不是留一个"有无 bias"的标志位——同一个思路，让下游 pass 只处理一种形态。
>
> **代价**：AST 和源码不再一一对应。`f(3)` 的树里有一个源码中不存在的 `self` 节点。所以做 IDE 工具（要精确定位源码位置）的时候，这种合成节点要特别标注"它是合成的"。我们的编译器不做 IDE，所以无所谓。

**一个真实的小问题**：合成的 `self` 节点 `line` 取的是 `t.line`（也就是 `f` 所在行）。这没错，但如果类型检查在这个 `self` 上报错，用户会看到指向 `f(3)` 那行的错误，说"self 的类型不对"——而用户源码里根本没写 `self`。这是 desugaring 的固有代价，工业编译器靠"合成节点标记"来改善报错措辞。

### 3.4 复合结构：`parseLet` 的列表处理

```cool
let x : Int <- 1, y : Int, z : String <- "a" in body
```

文法：`LET ID ':' TYPEID ['<-' E0] (',' ID ':' TYPEID ['<-' E0])* IN E0`

```cpp
std::unique_ptr<Expr> Parser::parseLet() {
  Token t = cur_; advance();                    // 吃掉 let
  auto n = std::make_unique<Let>(); n->line = t.line;
  for (;;) {                                    // ★ 至少一个绑定
    Binding b;
    b.name = expect(Tok::OBJECTID, "variable name").lexeme;
    expect(Tok::COLON, "':'");
    b.type = expect(Tok::TYPEID, "type name").lexeme;
    if (at(Tok::ASSIGN)) { advance(); b.init = parseExpr(); }   // 可选初值
    n->bindings.push_back(std::move(b));
    if (!at(Tok::COMMA)) break;                 // ★ 逗号决定是否继续
    advance();
  }
  expect(Tok::IN, "'in'");
  n->body = parseExpr();
  return n;
}
```

**`for(;;)` + 末尾 `if(!comma) break` 这个模式**是"一个或多个、逗号分隔"的标准写法。项目里出现四次：`parseLet` 的绑定、`parseFeature` 的形参、`parseDispatchArgs` 的实参、`parsePrimary` 的 self-dispatch 实参。

**对比"零个或多个"的写法**（`parseDispatchArgs`，`parser.cpp:377`）：

```cpp
std::vector<std::unique_ptr<Expr>> Parser::parseDispatchArgs() {
  expect(Tok::LPAREN, "'('");
  std::vector<std::unique_ptr<Expr>> args;
  if (!at(Tok::RPAREN)) {           // ★ 先判空：f() 是合法的
    for (;;) {
      args.push_back(parseExpr());
      if (!at(Tok::COMMA)) break;
      advance();
    }
  }
  expect(Tok::RPAREN, "')'");
  return args;
}
```

差别就是外面那个 `if (!at(RPAREN))`：**实参可以有 0 个（`f()`），let 绑定至少 1 个（`let in x` 非法）。** 文法里 `(…)*` 和 `(…)+` 的区别，在代码里就是这一个 `if`。

### 3.5 `parseDispatch`：后缀运算符为什么用循环

```cpp
std::unique_ptr<Expr> Parser::parseDispatch() {
  auto e = parsePrimary();
  for (;;) {
    if (at(Tok::DOT)) {
      Token t = cur_; advance();
      std::string method = expect(Tok::OBJECTID, "method name").lexeme;
      auto d = std::make_unique<Dispatch>();
      d->line = t.line;
      d->receiver = std::move(e);        // ★ 旧树 → 接收者
      d->method = method;
      d->args = parseDispatchArgs();
      e = std::move(d);                  // ★ 累加器更新
    } else if (at(Tok::AT)) {
      /* 静态分派 e@T.f(args)，结构同上 */
    } else break;
  }
  return e;
}
```

**和 `parseAddSub` 的循环结构完全同构**（`03a` §4.6 那个）：

| | `parseAddSub` | `parseDispatch` |
|---|---|---|
| 旧树放哪 | `b->lhs` | `d->receiver` |
| 累加器 | `lhs = std::move(b)` | `e = std::move(d)` |
| 循环条件 | `at(PLUS) \|\| at(MINUS)` | `at(DOT) \|\| at(AT)` |
| 树形 | 向左长 | 向左长 |

`a.f().g()` 的树：

```
Dispatch(.g)
└─ receiver: Dispatch(.f)
              └─ receiver: Object(a)
```

**`a.f()` 整体成为 `.g` 的接收者** → 向左长 → 求值顺序是"先 `a`，再 `.f()`，再 `.g()`"，符合直觉。

> **`03a` §4.7 那张表可以扩一行了**：后缀运算符和左结合中缀运算符**用同一套循环+累加器结构**，因为它们的树形要求相同（向左长）。这解释了为什么 `03b` §3.2 的五个模板里，左结合和后缀的模板都是 `( … )*`。

---

## 4. 动手写：两个函数

### 4.1 任务 A：`parseIf`

文法：`IF E0 THEN E0 ELSE E0 FI`

```cpp
std::unique_ptr<Expr> Parser::parseIf() {
  // 你来写
}
```

**自查清单**：
- [ ] 进来时 `cur_` 是 `IF`，你**先 `advance()`** 了吗？（`03a` §4.2 的契约）
- [ ] 三个子表达式调的都是 `parseExpr()`（回到 `E0`）而不是 `parsePrimary()`？为什么？
- [ ] `THEN`/`ELSE`/`FI` 用 `expect` 而不是 `at`+`advance`？（差别是什么？）
- [ ] `line` 取的是 `IF` 的行号？

<details>
<summary>参考答案（parser.cpp:283）</summary>

```cpp
std::unique_ptr<Expr> Parser::parseIf() {
  Token t = cur_; advance();              // 吃掉 if → 满足契约
  auto n = std::make_unique<If>(); n->line = t.line;
  n->cond = parseExpr();                  // 回到 E0：条件可以是任意表达式
  expect(Tok::THEN, "'then'");
  n->then_ = parseExpr();
  expect(Tok::ELSE, "'else'");
  n->else_ = parseExpr();
  expect(Tok::FI, "'fi'");
  return n;
}
```

**三个 `parseExpr()` 的理由**：`if x <- 1 then …` 里条件是个赋值（级 0），只有回到 `E0` 才能解析。这和 `03b` §3.3 括号回到 `E0` 是同一个道理——**任何"有明确边界包裹"的子表达式位置，都回到最松层**。

`expect` vs `at`+`advance` 的差别：`expect` 在不匹配时**报错并指出期望什么**（`expected 'then' but got …`），这是错误信息质量的来源。手写 parser 的优势就在这些地方。

</details>

### 4.2 任务 B：`parseCase`

文法：`CASE E0 OF (ID ':' TYPEID '=>' E0 ';')+ ESAC`

注意：分支是**一个或多个**，每个分支以 `;` 结尾（不是分隔符，是**结束符**）。

```cpp
std::unique_ptr<Expr> Parser::parseCase() {
  // 你来写
  // 提示：循环终止条件用什么？回想 03b §2.7 的 FOLLOW 思路
}
```

**自查清单**：
- [ ] 循环条件是 `while (!at(Tok::ESAC))`？（FOLLOW 思路：`ESAC` 是终止信号）
- [ ] 每个分支末尾 `expect(SEMI)`？（`;` 是结束符，所以在循环**内部**）
- [ ] `CaseBranch` 的三个字段都填了？

<details>
<summary>参考答案（parser.cpp:337）</summary>

```cpp
std::unique_ptr<Expr> Parser::parseCase() {
  Token t = cur_; advance();                  // 吃掉 case
  auto n = std::make_unique<Case>(); n->line = t.line;
  n->scrutinee = parseExpr();
  expect(Tok::OF, "'of'");
  while (!at(Tok::ESAC)) {                    // ★ ESAC 是终止信号
    CaseBranch b;
    b.name = expect(Tok::OBJECTID, "branch variable").lexeme;
    expect(Tok::COLON, "':'");
    b.type = expect(Tok::TYPEID, "branch type").lexeme;
    expect(Tok::DARROW, "'=>'");
    b.body = parseExpr();
    expect(Tok::SEMI, "';'");                 // ★ 结束符，在循环内
    n->branches.push_back(std::move(b));
  }
  expect(Tok::ESAC, "'esac'");
  return n;
}
```

**和 `parseLet` 的对照值得看**：

| | 分隔方式 | 循环形态 |
|---|---|---|
| `parseLet` 绑定 | `,` **分隔**（最后一个后面没有） | `for(;;)` + 末尾 `if(!comma) break` |
| `parseCase` 分支 | `;` **结束**（每个后面都有） | `while(!at(ESAC))` + 循环内 `expect(SEMI)` |

**"分隔符"和"结束符"导出两种不同的循环结构。** 这个区分在写任何 parser 时都会反复遇到（JSON 的 `,` 是分隔符，C 的 `;` 是结束符）。

**注意 `parseCase` 这个实现没检查"至少一个分支"**：`case x of esac` 会被接受，建出一个 `branches` 为空的 `Case` 节点，而文法要求 `(…)+`。这是个真实的小缺陷，正确做法是循环后加 `if (n->branches.empty()) error("case requires at least one branch")`。`parseBlock` 有同样的问题（`{ }` 会被接受，但文法要求 `(expr ';')+`）。留作 §8 动手题。

</details>

---

## 5. CP2 验收

### 5.1 构建

```bat
cd C:\Users\donk\Desktop\compilers\coolc
build.bat
```

**注意**：我实测发现 `build.bat` 在你这台机器上跑不通，而且 `-O2` 产出的二进制有问题。详见 §7，那里有可用的替代命令。

### 5.2 跑 `factorial.cl`

```bat
coolc.exe tests\factorial.cl --dump
```

源码：

```cool
class Main inherits IO {
  fact(n : Int) : Int {
    if n = 0 then 1 else n * fact(n - 1) fi
  };
  main() : Object {
    {
      out_int(fact(5));
      out_string("\n");
    }
  };
};
```

**实测输出**（我在你机器上跑出来的，`-O0` 构建）：

```
program
  class Main inherits IO
    method fact(n : Int) : Int
      if
        binary =
          object n
          int 0
        int 1
        binary *
          object n
          dispatch .fact
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
        dispatch .out_string
          object self
          string "\n"
```

### 5.3 逐条验证树形（对照 `01` 篇 §5 优先级表）

| # | 要验证的 | 在输出里怎么看 | 为什么对 |
|---|---|---|---|
| 1 | `=` 是**相等**不是赋值 | `binary =`，不是 `assign` | `03b` §4.2：`nxt_` 不是 `ASSIGN` → 走 `parseCompare` |
| 2 | `n * fact(n-1)` 中 `fact(n-1)` **整块**做右操作数 | `binary *` 的第二个孩子是 `dispatch .fact` | 分派（级 7）比 `*`（级 4）紧 → `parseMulDiv` 的 `rhs` 下钻时把整个分派吃掉 |
| 3 | `n - 1` 是 `fact` 的**实参**，不是 `fact(n)` 再减 1 | `binary -` 在 `dispatch .fact` 的孩子里 | `parseDispatchArgs` 里调 `parseExpr()` 回到 `E0`，把 `n-1` 整个吃掉 |
| 4 | self-dispatch 合成了 `self` | `dispatch .fact` 下面第一个孩子是 `object self` | §3.3 的 desugaring |
| 5 | `out_int(fact(5))` 两层嵌套 | `dispatch .out_int` → `dispatch .fact` → `int 5` | 实参位置递归回 `E0` |
| 6 | block 保留了两条语句的顺序 | `block` 下两个 `dispatch` 兄弟节点 | `parseBlock` 的 `while(!at(RBRACE))` |
| 7 | 字符串**没有**翻译转义 | `string "\n"`（两个字符，不是换行） | `02` 篇的 lexer 决策：`StringConst.value` 存词素原文，转义留给 `09` codegen 处理 |

**7 条全对 → CP2 达成。**

### 5.4 自己再验三个

```bat
coolc.exe tests\list.cl --dump
coolc.exe tests\case_test.cl --dump
coolc.exe tests\static_dispatch.cl --dump
```

看点：`list.cl` 的动态分派链、`case_test.cl` 的多分支、`static_dispatch.cl` 的 `static-dispatch @T.m` 输出。

---

## 6. 工业接轨

### 6.1 三个真实编译器的 AST 设计

| | 节点表示 | 遍历机制 | 备注 |
|---|---|---|---|
| **Clang** | `Stmt`/`Expr`/`Decl` 三大类层次，数百种节点 | `RecursiveASTVisitor`（CRTP 模板，不是虚函数） | 用 CRTP 避免虚调用开销；节点种类多到必须代码生成（`.td` 文件） |
| **rustc** | `ast::Expr` → 降级到 `hir::Expr` → 再到 `mir` | `Visitor` trait | **多层 IR**：AST 保留语法细节，HIR 已 desugar，MIR 是控制流图 |
| **CPython** | `_ast.AST`，由 `Python.asdl` 声明式生成 | `NodeVisitor`（Python 层）/ `asdl_c.py` 生成 C 遍历 | 节点定义是**声明式**的，C 结构体和 Python 类都从 ASDL 生成 |

**共同点**：节点种类一旦上百，就没人手写 `ast.h` 了——全部改成"声明式定义 + 代码生成"。我们 18 种节点手写刚好，再多就该写生成器了。

> **rustc 的多层 IR 值得留意**：`AST → HIR → MIR` 每层丢掉一些语法细节、加上一些分析信息。这和 `07` 篇要讲的"AST → LLVM IR"是同一个思路的不同粒度。**"每层 IR 只服务于特定的分析需求"** 是现代编译器的核心组织原则，`13` 篇的 MLIR 把它推到了极致（任意多层 dialect）。

### 6.2 对你的方向：AST 就是图变换的操作对象

你在 AI Infra 做的很多事，换个词就是编译器里的标准概念：

| 你做的 | 编译器术语 | 在哪篇 |
|---|---|---|
| 识别 `silu(gate) ⊙ up` 换成一个 kernel | **树/图重写**（pattern rewriting） | 本篇建的树 + `10` 篇的变换 |
| `torch.compile` 的 FX graph | AST 的近亲（有向图版） | `07` IR |
| Triton `@jit` 读 Python `ast` 模块 | **就是本篇这棵树** | 本篇 |
| TVM 的 `te.compute` → TIR lowering | 多层 IR 降级 | `07`、`13` |

**Triton 那条最直接**：你写的 `@triton.jit` kernel，Triton 用 Python 的 `ast.parse` 拿到 AST，然后一个 Visitor 遍历它生成 Triton IR → MLIR → LLVM IR → PTX。

> 所以这篇讲的 `ExprVisitor`，和 Triton 里那个遍历 Python AST 生成 IR 的类，**是同一个设计模式的同一种用法**。你以后读 Triton 的 `code_generator.py`，会看到一个巨大的 `CodeGenerator(ast.NodeVisitor)` 类，里面全是 `visit_BinOp`、`visit_Call`、`visit_For`——和 `06` 篇你要写的 `TypeChecker : public ExprVisitor` 结构完全一致。

---

## 7. 我实测发现的两个真实问题

写这篇时我在你机器上实际构建并运行了 `coolc`，发现两件与笔记内容无关但你需要知道的事。

### 7.1 `parseIsvoid` 拒绝合法程序（`03b` §3.5 的实测确认）

**已验证**。三个对照实验：

| 输入 | 实测结果 | 应该 |
|---|---|---|
| `isvoid isvoid x` | `ERROR: line 3: unexpected token 'isvoid'` | 合法（`expr ::= isvoid expr`） |
| `not not b` | 正常，建出两层 `not` | 合法 ✓ |
| `~~n` | 正常，建出两层 `neg` | 合法 ✓ |

同为前缀运算符，`not` 和 `~` 递归同层所以能嵌套，`isvoid` 调的是下一层 `parseNeg()` 所以不能。修法一个词：

```cpp
// parser.cpp:159
iv->expr = parseIsvoid();     // 原来是 parseNeg()
```

性质是**过严**（拒绝合法输入），不是过松。实践中没人写 `isvoid isvoid x`（第一个返回 `Bool`，套第二层恒为 `false`），所以一直没被发现。

### 7.2 你的构建环境有问题（这个更要紧）

**`build.bat` 现在跑不通。** 两个独立的故障：

**① g++ 的 `cc1plus` 已损坏。** 即使编译 `int main(){return 0;}` 也失败——退出码 1、零错误输出、不产出任何文件（连 `.o` 都不生成）。`-v` 显示它在启动 `cc1plus.exe` 后立刻静默死亡。

你的 `~` 目录下有 `sangfor/`（深信服终端安全），这类 EDR 拦截编译器写临时文件是常见现象。我试过改 `TMPDIR`、用 `-pipe` 绕开临时文件，都不行。

**② clang++ 能编译，但 `-O1`/`-O2` 产出的二进制在退出时段错误。**

| 构建 | 结果 |
|---|---|
| `clang++ -O0 -g` | **完全正常**，25 行 AST 输出，退出码 0 |
| `clang++ -O1` | 段错误，输出全丢（退出码 139） |
| `clang++ -O2` | 段错误，输出全丢 |
| `-O1` 在 gdb 下 | **正常跑完**，输出完整，"exited normally" |

关键证据：`coolc.exe /tmp/nope.cl`（不存在的文件）会**先正确打印** `cannot open: …` **然后**段错误。那条路径 `main` 里只是 `return 2`，parser 根本没启动。说明崩溃发生在 **`main` 返回之后的进程退出阶段**，不是编译器逻辑里。`--dump` 时输出全丢，是因为进程死在 stdout 刷新之前。

所以这是**工具链/运行时问题，不是 `coolc` 的代码问题**——AST 构建逻辑在 `-O0` 下完全正确。

**现在可用的构建命令**：

```bash
clang++ -std=c++17 -O0 -g -o coolc.exe src/*.cpp
```

**建议**：`13` 篇之前你要装 LLVM（`index.md` §5 已列为待办）。装完用完整的 clang 工具链，顺便就绕开了 g++ 这个坑。这件事我没动手改你的 `build.bat`——要不要改、怎么改（换 clang 还是修 g++），你定。

---

## 8. 动手题

1. **（必做）** 画出 `x.f(1).g(2, 3)` 的 AST。用 `--dump` 验证。
2. **（必做）** 为什么 `ast.h` 里 `Object` 节点既表示普通变量又表示 `self`？如果给 `self` 单独设一个 `SelfExpr` 节点，`06` 和 `09` 会更简单还是更复杂？
3. `NoExpr` 节点在 `ast.h` 里有定义，但 `parser.cpp` **从不创建它**（我搜过）。它是给谁准备的？（提示：看 `ast.h:137` 的 `Attribute::init` 注释和 `06` 会怎么处理无初值属性）
4. 把 `Dumper` 的遍历顺序从前序改成后序（先打孩子再打自己），输出会变成什么样？为什么打印用前序而 codegen 用后序？
5. **（修 bug）** 按 §7.1 修 `parseIsvoid`，写 `.cl` 验证 `isvoid isvoid x` 能建出两层 `isvoid`。
6. **（修 bug）** §4.2 提到 `parseCase` 接受零分支、`parseBlock` 接受空块，都违反文法的 `(…)+`。写出修法并验证 `case x of esac` 和 `{ }` 报出合理错误。
7. 给 `Feature` 加 `FeatureVisitor`，把 `dump.cpp` 的 `dynamic_cast` 换掉。做完回答：代码变长了还是变短了？你认为值得吗？（§2.6 的判断是不值得，你可以不同意）
8. `parsePrimary` 的 `default` 分支报 `unexpected token 'X'`。这个信息对用户友好吗？怎么改进？（提示：你手上有 `FIRST(E8)` 这个集合）
9. 如果把 `parsePrimary` 的 `LPAREN` 分支改成建一个 `Paren` 节点包起来，后面 `06`/`09` 要改什么？什么场景下这么做是**对**的？
10. **（综合）** 给 Cool 加 `for i : Int <- 0 to n loop body pool`。需要：① lexer 加什么 token（`02` 篇）；② 文法产生式；③ `ast.h` 加什么节点；④ `ExprVisitor` 和三个实现怎么改；⑤ 或者——能不能 **desugar 成已有的 `let` + `while`**，完全不加节点？对比两种方案。

> 第 10 题的第 ⑤ 问是重点。desugaring 能让你**零成本**加语法糖（不碰 `06`/`09`），代价是报错信息会指向用户没写过的构造。**这是真实编译器每天在做的权衡。**

---

## 9. 术语表

| 术语 | 一句话 |
|---|---|
| parse tree / CST | 忠实记录每条产生式、每个符号的树；格式化和重构工具需要它 |
| AST | 只保留后续阶段会读的信息；判据是"`05`/`06`/`09` 要不要用它" |
| 装饰 AST | 各阶段往节点上挂标注（如 `Expr::type` 由 `06` 填、`09` 读） |
| Visitor 模式 | 用 `accept` + 重载 `visit` 模拟双分派，把"操作"从"数据结构"里分离出去 |
| 双分派 | 行为同时依赖两个对象的动态类型（节点类型 × 遍历者类型） |
| 表达式问题 | 静态类型语言无法同时让"加类型"和"加操作"都廉价；Visitor 选择让加操作廉价 |
| desugaring | 把表面语法归一化成内部统一形式（`f(x)` → `self.f(x)`），减少下游分支 |
| 左因子分解 | 提出公共前缀消除 FIRST 冲突；适用于"分支都留在本层"的情形 |
| 分隔符 vs 结束符 | `,` 分隔（末项后无）→ `for(;;)+break`；`;` 结束（每项后有）→ `while(!终止符)` |
| 独占所有权 | `unique_ptr` 表达"每个节点恰好一个父亲"，树析构自动递归 |

---

## 10. 串联表

| 本篇内容 | 关联 | 换乘逻辑 |
|---|---|---|
| 丢掉穿透节点（§1.2） | `03b` §5.3 的 8 层空穿透 | 那 8 层调用在树上就是这些冗余节点，AST 把它们压掉 |
| 括号不建节点（§1.2） | `03b` §3.3 | 上篇讲"为什么回到 E0"，本篇讲"为什么不留痕迹" |
| `Expr::line`（§1.3） | `06` 类型错误定位、`09` 调试信息 | 源码文本被丢弃后，唯一的位置线索 |
| `Expr::type`（§1.3） | `06` 填 / `09` 读 | AST 作为跨阶段共享数据结构的证据 |
| Visitor（§2） | `06` TypeChecker、`09` CodeGen | 这两个类都是 `ExprVisitor` 的实现；本篇的 `Dumper` 是第一个 |
| 遍历顺序（§2.7） | `03b` §3.4 深度=求值顺序、`09` codegen | codegen 后序遍历，把树深度转化成指令顺序 |
| 左因子分解（§3.2） | `03b` §2.4 两处 LL(1) 冲突 | 同源冲突的第二种修法，判据是分支归属层级 |
| desugaring（§3.3） | `06`/`09` 少写分支；`13` 篇 MLIR | 编译器各层都在做的归一化 |
| 后缀=循环（§3.5） | `03a` §4.7 结合性表 | 后缀与左结合中缀同构，都是循环+累加器 |
| `unique_ptr` 所有权（§2.8） | 你 W4 的显存缓冲管理 | 同一个心智模型：谁持有、谁释放、转移而非复制 |
| CP2（§5） | `index.md` §4 检查点 | 前端第二关；下一关 CP3 在 `06` 末 |

---

## 下一步

**03 三篇闭环了。** 你现在应该能：从一份二义 BNF + 优先级表出发，推导出分层文法，判断需要几个 token 前瞻，写出完整的递归下降 parser，并设计一个供后续阶段复用的 AST。

接着读 `05a_符号表与作用域.md`（`04` 是 LR 理论支线，`05a` 才是主线的下一站）：

- **符号表与作用域** —— `let x : Int in x + 1` 里的 `x` 怎么和绑定关联起来；嵌套作用域怎么表示，以及这个数据结构如何从三条需求**推导**出来（而不是被告知）；
- **名字解析** —— 第一次在这棵树上跑真正的分析 pass；
- 然后是 `05b_类环境构建.md`：**类继承图** —— 检查循环继承、构建方法查找链，并回答"为什么语义分析必须分趟"，为 `06a`/`06b` 的类型检查铺路。

> **`04` 篇（LR/LALR）什么时候读？** 它是理论补全（自底向上分析、bison），和主线代码无关。你纯自学、目标是做出编译器，**可以先跳过，等 `09` codegen 跑通之后回来读**。那时你对"parser 到底在干什么"有了完整体感，读 LR 会轻松很多。如果你想现在读，也完全可以——它自成一体。
