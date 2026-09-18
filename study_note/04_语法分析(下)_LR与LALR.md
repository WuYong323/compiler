# 04 · 语法分析（下）：自底向上的 LR(0)/SLR/LR(1)/LALR 与 bison

- 主题：compilers
- 状态：[精]
- 标签：#编译原理 #CS143 #语法分析 #LR #LALR #yacc #bison
- 一句话结论：LR 分析是"边读入边压栈、时机成熟就归约"的**自底向上**算法，LR(1)/LALR(1) 是 yacc/bison 的引擎，也是 CS143 的必考理论。
- 相关笔记：`03_语法分析(上).md`（自顶向下）、`01_Cool语言精讲.md`（文法）
- 产出：`projects/coolc/cool.y`（bison 参考实现，对应 CS143 PA2，非主编译器）

---

## 精炼段

递归下降（doc 03）是"自顶向下"：从开始符号往下猜。LR 则是"自底向上"：把输入**逐步压栈**，当栈顶恰好是某产生式右部时**归约**成左部。它接受的文法类比 LL 更广（左递归天然没问题），代价是实现靠一张**自动生成的表**（而非手写函数）。LR 家族按"前瞻能力"从弱到强排：**LR(0) < SLR(1) < LALR(1) < LR(1)**；其中 LALR(1) 状态数和 SLR 一样少、表达能力接近 LR(1)，是 yacc/bison 的选择。CS143 原课 PA2 就用 bison 写 parser——本文补全这条理论线。

---

## 1. 移进-归约分析（Shift-Reduce）——LR 的核心机制

LR 分析器 = **一个栈 + 一张 ACTION/GOTO 表**。四个动作：
- **移进（shift）**：读入下一个 token，压栈；
- **归约（reduce）**：栈顶匹配某产生式 `A → β` 的右部，弹出 `|β|` 个符号、压入 `A`；
- **接受（accept）**：栈顶是开始符号且输入读完；
- **报错**：无合法动作。

**直觉**：就像"搭积木"——先把碎片（token）一个个放上桌，凑成一块"组件"就合成一个大件，最后合成整座塔。

**例子**：文法 `E → E + E | E * E | id`（为说明先忽略优先级），分析 `id + id`：

| 栈 | 输入 | 动作 |
|---|---|---|
| | `id + id $` | shift id |
| id | `+ id $` | reduce `E → id` |
| E | `+ id $` | shift + |
| E + | `id $` | shift id |
| E + id | `$` | reduce `E → id` |
| E + E | `$` | reduce `E → E + E` |
| E | `$` | accept |

`$` 是输入结束标记。

---

## 2. LR(0) 项与 LR(0) 自动机——"我们读到了产生式的哪一步"

### 2.1 LR(0) 项（item）

一个 **LR(0) 项** = 在产生式右部某处打一个**点**，表示"点左边已归约、点右边待读"：

```
E → E · + E      点前是 E，接下来期望 "+ E"
E → E + · E      已经读到 "E +"
E → E + E ·      整个右部读完，可以归约了
```

### 2.2 两个核心运算

- **闭包 closure(I)**：若项 `A → α · B β` 在 I 中（点后是非终结符 B），则把 B 的所有产生式 `B → · γ` 也加入 I。
- **转移 goto(I, X)**：I 中所有 `A → α · X β` 的点右移一格，再取闭包。

### 2.3 LR(0) 自动机 = 状态图

状态 = 项集（闭包后的集合），边 = goto。以文法 `E → E + T | T; T → id | (E)` 为例，起始态 I0 = closure(`E → · E + T`, `E → · T`, ...)：

```
I0: E'→·E    E→·E+T   E→·T   T→·id   T→·(E)
      |--E--> I1: E'→E·   E→E·+T
      |--T--> I2: E→T·
      |--id-> I3: T→id·
      |--(--> I4: T→(·E)  E→·E+T  E→·T  T→·id  T→·(E)
I1 --+--> I5: E→E+·T  T→·id  T→·(E)
    ...
```

**意义**：这张自动机就是 LR 分析表的骨架——状态决定"该 shift 还是 reduce"。

---

## 3. 冲突：什么时候"移进/归约"会打架

LR 分析可能遇到两类冲突（某个状态里）：
1. **移进-归约冲突**：一个项说"该归约"（点在末尾），另一个说"该移进"（点后有终结符）。例：`E → E · + E`（移进 `+`）vs `E → E ·`（归约）。
2. **归约-归约冲突**：两个不同产生式都在点末尾。

**直觉**：状态里有"点末尾的项"= 候选归约；有"点后是终结符的项"= 候选移进。若两者并存且无法区分，就冲突。**LR 各变体的区别，就是"用多少前瞻信息来化解冲突"**。

---

## 4. 从 LR(0) 到 SLR(1)：用 FOLLOW 化解冲突

**LR(0)** 完全不看前瞻，冲突最多，能用的文法很少。

**SLR(1)**（Simple LR）：归约 `A → α` **只在下一个 token ∈ FOLLOW(A) 时才进行**。用文法 `S → L = R | R; L → *R | id; R → L`（经典例子）说明 SLR 的不足：

- 在某个状态里，有项 `S → L · = R`（想 shift `=`）和 `R → L ·`（想归约）。
- SLR 看 FOLLOW(R) = {=, $}，里面包含 `=`，所以 SLR 仍然**冲突**（不知道该 shift `=` 还是按 `R→L` 归约）。
- 但实际上，`L = R` 场景里，`L` 后面跟 `=` 时**绝不该**归约成 R——SLR 的 FOLLOW 太粗糙。

**结论**：SLR 比 LR(0) 强，但 FOLLOW 是"静态的、与上下文无关的"，仍不够精确。

---

## 5. LR(1)：给每个项带上"前瞻"

**LR(1) 项**：`[A → α · β, a]`，其中 `a` 是一个**前瞻 token**——表示"归约 `A → α` 后，下一个 token 必须是 `a`"。

- 前瞻的来源：从项 `[A → α · B β, b]` 出发，B 的新项 `[B → · γ, c]` 的前瞻 `c ∈ FIRST(β b)`——即"B 后面可能跟什么"。
- 于是 `[R → L ·, $]` 和 `[R → L ·, =]` 是**两个不同的 LR(1) 项**，能精确区分"什么时候该归约 R"。

**代价**：LR(1) 状态数可能**爆炸**（比 SLR 多一个数量级），工程上不可接受。

---

## 6. LALR(1)：合并"同核"状态——yacc/bison 的选择

**LALR(1)**（Look-Ahead LR）：把 LR(1) 中**核心相同**（去掉前瞻后相同的项集）的状态**合并**。

- 状态数 = SLR 的状态数（少）；
- 表达力介于 SLR 和 LR(1) 之间（比 SLR 强，接近 LR(1)）；
- 合并可能**引入归约-归约冲突**（但绝不引入移进-归约冲突），实际文法很少触发。

**一句话总结家族关系**：

```
LR(0)  ⊂  SLR(1)  ⊂  LALR(1)  ⊂  LR(1)
（接受的文法类由小到大；状态数：SLR≈LALR 少，LR(1) 多）
```

> 考试常问：① 给文法判断是否是 SLR(1)/LR(1)；② 构造 LR(0) 自动机；③ 说明某冲突是移进-归约还是归约-归约；④ 为什么 LALR 会合并状态。

---

## 7. yacc/bison 参考实现（对应 CS143 PA2）

CS143 原课 PA2 用 bison 写 parser。核心是：**用 `%left/%right/%nonassoc` 声明优先级和结合性**（顺序从低到高），bison 据此自动化解移进-归约冲突。

> 我们主编译器手写递归下降（doc 03），下面是等价 bison 参考（`cool.y` 节选），仅供对照理解 PA2：

```yacc
%{
  /* 头文件、AST 构建代码（省略） */
%}
%token CLASS INHERITS IF THEN ELSE FI WHILE LOOP POOL LET IN CASE OF ESAC
%token NEW ISVOID NOT TRUE FALSE TYPEID OBJECTID INT_CONST STR_CONST
%token ASSIGN DARROW LE
%right ASSIGN            /* 赋值 <-：最低优先级（声明顺序从低到高） */
%left NOT                /* not */
%nonassoc '=' '<' LE     /* 比较：非结合 */
%left '+' '-'            /* 加减：左结合 */
%left '*' '/'            /* 乘除：左结合 */
%right ISVOID            /* isvoid 前缀 */
%right '~'               /* 取负 ~：最高（分派 . @ 由文法结构自然最紧） */

%%
program : class_list          { /* 构建 Program */ }
        ;
class_list : class_list class | class ;
class : CLASS TYPEID ';'
      | CLASS TYPEID INHERITS TYPEID ';'
      | CLASS TYPEID '{' feature_list '}' ';'
      | CLASS TYPEID INHERITS TYPEID '{' feature_list '}' ';'
      ;
feature_list : feature_list feature ';' | /* empty */ ;
feature : OBJECTID '(' formal_list ')' ':' TYPEID '{' expr '}'
        | OBJECTID ':' TYPEID
        | OBJECTID ':' TYPEID ASSIGN expr
        ;
expr : OBJECTID ASSIGN expr                        /* 赋值，右结合 */
     | expr '.' OBJECTID '(' args ')'              /* 动态分派 */
     | expr '@' TYPEID '.' OBJECTID '(' args ')'   /* 静态分派 */
     | OBJECTID '(' args ')'                       /* self 分派 */
     | IF expr THEN expr ELSE expr FI
     | WHILE expr LOOP expr POOL
     | '{' block_list '}'
     | LET let_list IN expr
     | CASE expr OF case_list ESAC
     | NEW TYPEID
     | ISVOID expr
     | expr '+' expr | expr '-' expr | expr '*' expr | expr '/' expr
     | '~' expr
     | expr '<' expr | expr LE expr | expr '=' expr
     | NOT expr
     | '(' expr ')'
     | OBJECTID | INT_CONST | STR_CONST | TRUE | FALSE
     ;
%%
```

**关键点**：
- `%left/%right/%nonassoc` 的**声明顺序 = 优先级从低到高**；每个声明的 `%left`/`%right` 决定结合方向。
- Cool 的 `<= < =` 是**非结合** → `%nonassoc`；赋值 `<-` 是右结合 → `%right`。
- 前缀运算符（`not`、`isvoid`、`~`）也声明优先级，bison 用它判断"移进还是归约"。

---

## 8. LL vs LR 全景对比（把上、下两篇串起来）

| 维度 | LL（递归下降，doc 03） | LR（bison，doc 04） |
|---|---|---|
| 方向 | 自顶向下（从 S 展开） | 自底向上（归约到 S） |
| 左递归 | 必须消除 | 天然支持 |
| 文法类 | LL(1) 较小 | LR(1)/LALR 更大 |
| 前瞻 | 选产生式时看输入 | 归约时看前瞻 |
| 实现 | 手写函数，直观 | 表驱动，自动生成 |
| 错误诊断 | 优秀（可定制消息） | 较弱（"syntax error"） |
| 工业现状 | **主流**（Clang/Rust/Go/v8） | yacc 系遗留、gcc 早期 |

> **关键认知**：表达力上 LR ⊋ LL，但**工业界如今偏爱手写递归下降**，因为"可读性 + 错误信息质量"比"能多解析几类文法"更重要（现实语言都经过精心设计，文法足够"LL 友好"）。这正是我们主编译器手写、同时用 bison 作参考的理由。

---

## 9. 本文你掌握了什么 / 易错点 / 下一步

**掌握**：shift-reduce 机制、LR(0) 项/闭包/goto/自动机、两类冲突、SLR（FOLLOW 归约）、LR(1)（项带前瞻）、LALR（合并同核）、bison 的优先级声明、LL vs LR 对比。

**易错点**：
1. **SLR 用 FOLLOW、LR(1) 用项内前瞻**——SLR 的冲突 LR(1) 可能没有（这是考点）。
2. LALR 合并**只可能引入归约-归约冲突**，不会引入移进-归约冲突。
3. bison 里**后声明的优先级更高**，所以声明顺序要**从低优先级写到高优先级**：赋值 `<-` 最低 → 声明在最前，`~` 最高 → 声明在最后（见 §7）。
4. 别把"移进"（压 token）和"归约"（替换成非终结符）混为一谈。

**下一步**：读 `05_抽象语法树与语义分析.md`，开始理解程序**语义**——符号表、作用域、名字解析，为类型检查（doc 06）铺路。
