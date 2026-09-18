# 06 · 类型检查与 Cool 类型系统

- 主题：compilers
- 状态：[精]
- 标签：#编译原理 #CS143 #类型检查 #类型系统 #子类型 #SELF_TYPE #静态分析
- 一句话结论：类型检查用**类型环境（O, M, C）**和一组**类型规则**自底向上推断每个表达式的类型，并保证**静态类型是动态类型的上界**（soundness）。
- 相关笔记：`05_符号表.md`（环境来源）、`01_Cool语言精讲.md`（类型规则依据）
- 产出代码：`projects/coolc/src/typechecker.h`、`typechecker.cpp`

---

## 精炼段

类型检查（type checking）是语义分析的核心：给每个表达式**推断一个类型**，并检验所有操作的类型约束是否满足。它的理论根基是**类型系统**——一组推理规则（形如"若子表达式有类型 T₁…Tₙ，则整个表达式有类型 T"）。Cool 的关键规则有：**一致性（conformance）** `C ≤ P`（子类型关系，由继承图定义）、**SELF_TYPE**（随 self 动态类型变化的特殊类型）、以及**最小上界 ⊔**（if/case 两个分支类型的"合并"）。类型检查器的目标是**sound（健全）**：它推断的静态类型永远是运行时动态类型的上界——宁可拒绝一些"其实能跑"的程序，也绝不放过任何会出类型错误的程序。

---

## 1. 静态类型 vs 动态类型，与健全性（Soundness）

| | 静态类型（static type） | 动态类型（dynamic type） |
|---|---|---|
| 何时 | **编译期**由类型检查器推断 | **运行期**对象的真实类 |
| 记号 | `S_e` | `D_e` |
| 例子 | `let x : Object <- new Int...` 里 x 的静态类型是 Object | x 运行时是 Int |

**健全性（soundness）**：对任意表达式 e，必须 `D_e ≤ S_e`（动态类型 conform 到静态类型）。

**一句话直觉**：编译器**宁可高估**类型（把 `new Cons` 当 `List` 看），也**不能低估**（把 `List` 当 `Int` 看）。高估只会拒绝一些"其实安全"的程序（保守），低估则会放过真正的类型错误（危险）。健全 = 保守但安全。

---

## 2. 类型环境（Type Environment）：O, M, C

类型检查需要**上下文**（哪些名字是什么类型）。Cool 用三元组 `O, M, C`：

| 符号 | 含义 | 我们的实现 |
|---|---|---|
| `O` | 对象环境：变量名 → 类型 | `SymbolTable O_`（作用域栈，doc 05） |
| `M` | 方法环境：`(类, 方法) → 签名` | `methodEnv_`（含继承/重写检查） |
| `C` | 当前类（处理 SELF_TYPE 需要） | `currentClass_` |

**推理规则记法**（手册原文）：
```
  前提条件（子表达式的类型判断）
───────────────────────────────
  O,M,C ⊢ e : T   （读作：环境 O,M,C 下，表达式 e 有类型 T）
```

---

## 3. 一致性 Conformance（`≤`）——子类型关系

`C ≤ P` 读作"C **conform** 到 P"或"C 是 P 的子类型"，当且仅当：C 继承自 P（直接或间接），或 C = P。

```
A ≤ A                                （自反）
C ≤ P    若 C inherits P              （继承）
A ≤ P    若 A ≤ C 且 C ≤ P            （传递）
A ≤ Object（Object 是根）             （任何类型 ≤ Object）
```

**直觉**：子类对象**能用在父类被要求的地方**（和 Java 向上转型一致）。`Cons ≤ List ≤ Object`，所以"期望 List"的地方可以放 Cons。

---

## 4. SELF_TYPE 的类型规则（Cool 最精妙处）

回顾 doc 01 §6.3：`SELF_TYPE` 指"self 的动态类型"。类型规则（记 `SELF_TYPE_C` = 类 C 里的 SELF_TYPE）：

```
SELF_TYPE_C ≤ P   当且仅当   C ≤ P
SELF_TYPE_C ≤ SELF_TYPE_C    （自反）
```

**为什么需要它**：让 `copy()` 这类方法能返回"和 self 一样精确的类型"，这样 `(new Sally).copy()` 的类型是 `Sally` 而不是 `Object`。

**实现要点**（我们的 `conforms`）：`a == "SELF_TYPE"` 时，把 a 换成 `currentClass_` 再比较；`b == "SELF_TYPE"` 且 `a != "SELF_TYPE"` 时，直接 false（只有 SELF_TYPE ≤ SELF_TYPE）。

---

## 5. 完整类型规则（手册的推理规则，逐条对照实现）

> 完整原文在 `refs/cool-manual-full.txt` "Type Checking Rules"。下面是每条规则 + 我们的实现函数。

| 规则 | 推理规则（简化） | 实现函数 |
|---|---|---|
| **[Var]** | `O(id)=T ⇒ id : T` | `visit(Object)`：`O_.lookup(name)` |
| **[Assign]** | `O(id)=T, e:T', T'≤T ⇒ id<-e : T'` | `visit(Assign)` |
| **[Int/String/Bool]** | 常量类型固定 | `visit(IntConst)` 等 |
| **[New]** | `new T : T`；`new SELF_TYPE : SELF_TYPE_C` | `visit(New)` |
| **[Dispatch]** | 见下（最复杂） | `visit(Dispatch)` |
| **[StaticDispatch]** | 同 Dispatch 但方法查 T | `visit(StaticDispatch)` |
| **[If]** | `cond:Bool, e2:T2, e3:T3 ⇒ if : T2⊔T3` | `visit(If)` |
| **[Loop]** | `cond:Bool ⇒ while : Object` | `visit(While)` |
| **[Sequence]** | `{e1..en} : Tn` | `visit(Block)` |
| **[Let]** | 初始化 conform 声明类型，体在新环境 | `visit(Let)` |
| **[Case]** | 各分支类型取 ⊔，分支类型须互异 | `visit(Case)` |
| **[Isvoid]** | `isvoid e : Bool` | `visit(IsVoid)` |
| **[Arith]** | `+ - * /` 操作数 Int，结果 Int | `visit(Binary)` |
| **[Equal]** | 基本类型须成对相同，结果 Bool | `visit(Binary)` |
| **[Neg/Not]** | `~e:Int`，`not e:Bool` | `visit(Neg)/visit(Not)` |

### 5.1 最重要的规则：**[Dispatch]**（动态分派）

```
O,M,C ⊢ e0:T0,  O,M,C ⊢ ei:Ti (i=1..n)
T0' = (T0 == SELF_TYPE_C) ? C : T0            -- 用动态类型找方法
M(T0', f) = (T1'...Tn', Tn+1')                -- 查方法签名
Ti ≤ Ti'  (i=1..n)                            -- 实参 conform 形参
Tn+1 = (Tn+1' == SELF_TYPE) ? T0 : Tn+1'      -- SELF_TYPE 返回 → 接收者类型
─────────────────────────────────────────────
O,M,C ⊢ e0.f(e1..en) : Tn+1
```

**关键三点**：
1. 方法查找用**接收者的（静态）类型** `T0'`（SELF_TYPE 则用当前类 C）；
2. 实参类型必须 conform 形参类型；
3. 若方法返回 `SELF_TYPE`，则整个调用的类型 = **接收者类型 T0**（不是 SELF_TYPE！）。

我们的 `visit(Dispatch)` 逐行实现了这三点。

### 5.2 最小上界 ⊔（lub）——if/case 用

`if` 的两个分支、`case` 的各分支，结果类型是各分支类型的**最小上界（join/⊔）**：

```
lub(X, X) = X
lub(SELF_TYPE_C, T) = lub(C, T)      -- SELF_TYPE 先展开成当前类
lub(A, B) = A、B 的最近公共祖先（LCA）
```

`Object` 是根，所以 LCA 总存在。例：`if flag then new Cons else new Nil fi : List`（Cons、Nil 的 LCA 是 List）。

---

## 6. 环境构建：继承、重写与硬性限制

类型检查**之前**要先建好类环境（`buildEnvironments`），这一步本身也是语义检查（CS143 PA3 的一部分）：

1. **内建类**：`Object/IO/Int/String/Bool` 的方法环境预置（`installBuiltins`，签名见 doc 01 §8）。
2. **注册类**：检查重复定义、重定义内建类（`Object/IO/Int/String/Bool` 不能重定义）。
3. **继承检查**：父类必须存在；`Int/String/Bool` 不能被子类继承。
4. **环检查**：继承图不能有环（DFS 检测）。
5. **Main 检查**：必须有 `Main` 类，且能提供无参 `main` 方法（可继承）。
6. **方法/属性环境**（`methodsOf`/`attrsOf`，递归 + 记忆化）：
   - 继承父类的方法/属性；
   - **方法重写**：签名（形参类型 + 返回类型）必须**完全一致**；
   - **属性重定义**：继承的属性**不能**被重定义；
   - 同类内方法名/属性名不能重复（但方法与属性可同名）。

---

## 7. 代码要点（完整实现见 `src/typechecker.cpp`）

```cpp
// TypeChecker 是 ExprVisitor，visit 里调用 typeOf(child) 取子表达式类型
std::string TypeChecker::typeOf(const Expr& e) { e.accept(*this); return result_; }

void TypeChecker::visit(const Dispatch& e) {
  std::string t0 = typeOf(*e.receiver);          // 接收者类型
  std::string t0p = resolveSelf(t0);             // SELF_TYPE → 当前类
  const auto& menv = methodsOf(t0p);
  auto it = menv.find(e.method);
  if (it == menv.end()) { error(...); return; }
  // 逐实参检查 conform 形参类型
  for (...) if (!conforms(typeOf(*arg), sig.argTypes[i])) error(...);
  // SELF_TYPE 返回 → 接收者类型
  result_ = (sig.retType == "SELF_TYPE") ? t0 : sig.retType;
}

void TypeChecker::checkFeature(const Feature& f) {
  O_.pushScope();                        // 属性作用域（外层）
  for (auto& [n, t] : attrsOf(currentClass_)) O_.add(n, t);
  O_.pushScope();                        // self + 形参（内层，遮蔽属性）
  O_.add("self", "SELF_TYPE");
  // ... 类型检查属性初始化 / 方法体，检查结果 conform 声明类型
}
```

**关键设计**：`conforms` 沿父链上溯判断子类型；`lub` 用祖先集合求 LCA；`self` 与形参在**内层**作用域（所以形参遮蔽同名属性，符合手册）。

---

## 8. 测试结果（CP3 达成）

| 程序 | 结果 |
|---|---|
| `hello.cl`、`factorial.cl`、`list.cl` | `OK: program type-checks successfully` |
| `type_error.cl`（`1 + "a"`） | `ERROR: line 3: arithmetic operands must be Int, got Int and String` |

`list.cl` 同时验证了：方法重写（`isNil/head/tail` 覆盖 `List` 的）、`SELF_TYPE` 返回（`init`）、动态分派（`sum` 里 `l.isNil()` 按 List 类型查方法）、`let`、`if`、递归。

---

## 9. 本文你掌握了什么 / 易错点 / 下一步

**掌握**：静态 vs 动态类型、soundness、类型环境 O/M/C、conformance、SELF_TYPE 规则、lub/⊔、完整类型规则、Dispatch 规则的三步、环境构建的六项检查、typechecker 实现。

**易错点**：
1. **Dispatch 返回 SELF_TYPE 时，结果 = 接收者类型 T0**，不是 "SELF_TYPE" 也不是声明返回类型。
2. `if`/`case` 结果用 **lub（⊔）**，不是随便取一个分支。
3. 方法**重写要求签名完全一致**，但属性**重定义是错误**。
4. `while` 类型恒为 **Object**（运行时是 void）。
5. `Int/String/Bool` 不可继承、不可重定义；`Object/IO` 不可重定义。
6. 形参**遮蔽**同名属性（内层作用域优先）。

**下一步**：读 `07_中间表示与LLVM_IR.md`，把带类型的 AST 翻译成**中间表示**——这是通往代码生成（doc 09）的关键桥梁，也是理解 LLVM/MLIR 的入口。
