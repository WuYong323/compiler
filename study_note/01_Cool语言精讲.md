# 01 · Cool 语言精讲：我们要编译的"源语言"

- 主题：compilers
- 状态：[精]
- 标签：#编译原理 #CS143 #Cool语言 #源语言 #面向对象
- 一句话结论：Cool 是一个为教学设计的**静态类型、单继承、面向对象**语言，语法类似"迷你版 Java/Scala"，小到足以手写实现，却包含真实语言的所有核心机制。
- 相关笔记：`00_课程地图与总览.md`、`refs/cool-manual-full.txt`（权威原文）
- 产出代码：`projects/coolc/tests/*.cl`

---

## 精炼段

Cool（Classroom Object Oriented Language）是 Stanford 专为 CS143 设计的源语言：它足够小（十几个关键字、一种继承、八种表达式），让你能手写全部前端；又足够真实（类、继承、动态分派、静态类型、子类型、SELF_TYPE、垃圾对象），让你学到面向对象语言的编译精髓。本文是后面词法/语法/语义/代码生成四篇的"题目"——必须先吃透它。核心记住三件事：**① 词法（token 怎么切）② 语法（BNF + 优先级）③ 类型与语义（conformance、动态分派、默认初始化）**。

---

## 1. 为什么用 Cool 教学（一句话直觉）

**类比**：学做菜不会直接上满汉全席，而是先做一道"番茄炒蛋"——工序齐全但规模可控。Cool 就是编译原理的"番茄炒蛋"：它有类、继承、方法、动态分派、类型检查，但去掉了真实语言（Java/C++）里一切会分散注意力的东西（包、泛型、异常、多继承、可见性修饰符、标准库）。**它麻雀虽小，五脏俱全。**

---

## 2. 一个 Cool 程序长什么样（先看整体，再看细节）

```cool
class Main inherits IO {
  greeting : String <- "Hello, world!\n";   -- 一个属性（attribute），带初始化

  main() : Object {                          -- 一个方法（method）
    out_string(greeting)                     -- 方法体：一次动态分派
  };
};
```

要点（先建立直觉，后面逐条展开）：
- 程序是**若干个 class**，其中一个必须叫 `Main`，且要能提供无参的 `main()` 方法。
- class 可**单继承**（`inherits IO`）；不写 `inherits` 默认继承 `Object`。
- class 内是若干 **feature**：要么是**属性**（`greeting : String <- ...`），要么是**方法**（`main() : Object { ... }`）。
- 程序入口 = 执行 `(new Main).main()`。

---

## 3. 词法结构（Lexical Structure）——token 怎么切

> 这一节是 doc 02（词法分析）的"需求文档"。lexer 的唯一职责就是把这些规则实现出来。

### 3.1 空白（White space）

空白包括这 6 个字符：空格(32)、`\n`(10)、`\f`(12)、`\r`(13)、`\t`(9)、`\v`(11)。lexer 遇到它们就跳过，只用于分隔 token。

### 3.2 注释（Comments）——有两种，且块注释可嵌套

```cool
-- 这是行注释：从 "--" 到本行行尾（或文件结尾）
(* 这是块注释 (* 可以嵌套 *) 结束 *)
```

- `--`：行注释，到行尾（或 EOF）为止。
- `(* ... *)`：块注释，**可嵌套**（这是 Cool 的经典考点，lexer 要计数配对）；不能包含 EOF。

### 3.3 关键字（Keywords）——共 19 个

```
class  else  false  fi  if  in  inherits  isvoid  let
loop   pool  then  while  case  esac  new  of  not  true
```

**大小写规则（易错点）**：关键字**不区分大小写**（`IF`、`If`、`iF` 都是 `if`），**但 `true`/`false` 例外**——它们首字母必须小写（`True`、`FALSE` 是**类型标识符**而不是关键字）。

### 3.4 标识符（Identifiers）——分两类，大小写敏感

| 种类 | 规则 | 例子 | 说明 |
|---|---|---|---|
| 类型标识符 TYPEID | 大写字母开头 + `[A-Za-z0-9_]*` | `Main`, `Int`, `Cons` | 类名、类型名 |
| 对象标识符 OBJECTID | 小写字母开头 + `[A-Za-z0-9_]*` | `main`, `x`, `foo_bar` | 变量名、方法名、属性名 |

两个**特殊标识符**（不是关键字，但语义特殊）：
- `self`：特殊的 OBJECTID，每个类里隐式绑定，指向当前对象。**可读不可写**（不能赋值给 self、不能在 let/case/形参里绑定 self、不能有名为 self 的属性）。
- `SELF_TYPE`：特殊的 TYPEID，表示"self 的动态类型"，见 §6.3。

### 3.5 整数（Integers）

- 非空数字串 `[0-9]+`，**恒为非负**。
- 合法范围 `0 ~ 2147483647`（32 位有符号整数的非负一半）。
- **超出范围是词法错误**（lexer 要报"integer constant too large"）。

### 3.6 字符串（Strings）——Cool 最"反直觉"的地方，务必精读

规则（Cool 的字符串转义和 C/Java **完全不同**）：

1. 字符串用双引号 `"..."` 括起来。
2. **只有两个转义序列**：`\t`（制表符）、`\n`（换行）——而且它们**是两个字符** `\`+`t`、`\`+`n` 存在字符串里，**由 IO/运行时解释，lexer 不翻译**。
3. `\` 后面跟**任何其他字符** `c`，就表示**字面的两个字符** `\` 和 `c`（没有特殊含义）。例如 `\r`、`\0`、`\"`、`\\` 都是"反斜杠 + 那个字符"两个字符。
4. 要在字符串里放一个 `"`，必须写成 `\"`，但 `\"` **不会变成** `"`，而是**保持** `\`+`"` 两个字符。例如 `"a\"b"` 的内容是 `a\"b`（5 个字符）。

```cool
out_string("She said, \"Hello.\"\n")
-- 输出：She said, \"Hello.\"  （然后是换行）
```

5. **非法情况（lexer 必须报错）**：
   - 字符串里出现**未转义的换行**（`"This is not OK` 跨行）→ 报错；
   - 字符串里出现 **EOF**（字符串不能跨文件边界）；
   - 字符串里出现 **NUL**（ASCII 0）；
   - 字符串字面量**超过 1024 个字符**。

> **为什么 Cool 这样设计**（而不是像 C 那样 `\n` 变一个字节）：为了让 lexer 简单——反斜杠就是"吃掉下一个字符"，无需查表转换。这是一种"教学上牺牲便利、换取实现简洁"的取舍，记住这个设计哲学，后面写 lexer 会非常轻松。

### 3.7 特殊符号（Special Notation）

单字符：`( ) { } : ; , . @ ~ * / + - < =`
多字符：`<-`（赋值）、`<=`（小于等于）、`=>`（case 分支箭头）

---

## 4. 语法（Grammar）——完整的 BNF

> 权威原文在 `refs/cool-manual-full.txt` 的 "Cool Syntax"。下面是完整文法（`[]` 表可选，`*` 表零次或多次，`+` 表一次或多次，`[[ ]]` 只是分组标记不是语言符号）。

```
program     ::= [[ class ; ]]+
class       ::= class TYPE [ inherits TYPE ] { [[ feature ; ]]* }
feature     ::= ID ( [ formal [, formal]* ] ) : TYPE { expr }
              | ID : TYPE [ <- expr ]
formal      ::= ID : TYPE
expr        ::= ID <- expr
              | expr [ @ TYPE ] . ID ( [ expr [, expr]* ] )
              | ID ( [ expr [, expr]* ] )
              | if expr then expr else expr fi
              | while expr loop expr pool
              | { [ expr ; ]+ }
              | let ID : TYPE [ <- expr ] [, ID : TYPE [ <- expr ]]* in expr
              | case expr of [ ID : TYPE => expr ; ]+ esac
              | new TYPE
              | isvoid expr
              | expr + expr | expr - expr | expr * expr | expr / expr
              | ~ expr
              | expr < expr | expr <= expr | expr = expr
              | not expr
              | ( expr )
              | ID | integer | string | true | false
```

### 4.1 逐条读懂这个文法（这是理解 Cool 的关键）

| 语法结构 | 含义 | 例子 |
|---|---|---|
| `ID <- expr` | **赋值**（`<-` 是赋值，不是 `=`） | `x <- 5` |
| `expr.ID(args)` | **动态分派**（运行时按对象真实类型找方法） | `l.head()` |
| `expr@TYPE.ID(args)` | **静态分派**（强制用 TYPE 类的方法） | `e@IO.out_string(s)` |
| `ID(args)` | **self 分派**（等价 `self.ID(args)`） | `fact(n-1)` |
| `if e1 then e2 else e3 fi` | 条件（**注意结束符是 `fi`**） | `if n=0 then 1 else n*f fi` |
| `while e1 loop e2 pool` | 循环（结束符 `pool`） | `while i<n loop i<-i+1 pool` |
| `{ e1; e2; ...; en; }` | **块**（顺序求值，值 = 最后一个） | 见示例 |
| `let x:T <- e, y:T in body` | **let** 引入局部变量 | `let x:Int <- 3 in x+1` |
| `case e of x:T => e; ... esac` | **case** 按运行时类型分派 | 见 §7.6 |
| `new T` | 创建对象 | `new Cons` |
| `isvoid e` | 测试是否为 void（空） | `isvoid x` |
| `+ - * /` | 整数算术（**整除**） | `7 / 2` → `3` |
| `~ e` | 整数取负（补） | `~x` |
| `< <= =` | 比较（`=` 是相等，不是赋值！） | `a = b` |
| `not e` | 布尔取反 | `not flag` |

> **两个极易混的点**：① 赋值是 `<-`，相等是 `=`；② 结束符很特别——`if...fi`、`while...pool`、`case...esac`。

---

## 5. 优先级与结合性（Precedence & Associativity）

从**最高**到**最低**：

```
.  @                       （分派，绑得最紧）
~
isvoid
*  /
+  -
<=  <  =
not
<-                         （赋值，绑得最松）
```

**结合性**：
- 所有二元运算符**左结合**，**除了**：赋值 `<-` 是**右结合**；三个比较 `<= < =` **不结合**（即 `a < b < c` 是语法错误）。

**优先级怎么影响解析**（doc 03 会详细展开，这里先建立直觉）：
- `~x * y` 先算 `~x`（`~` 比 `*` 紧）再乘。
- `not x = y` 解析为 `not (x = y)`（`=` 比 `not` 紧）。
- `a <- b <- c` 解析为 `a <- (b <- c)`（`<-` 右结合）。

---

## 6. 类型系统（Types）——静态类型的核心

### 6.1 每个类名都是一个类型

类型 = 类名（`Int`、`String`、`Bool`、`Object`、你自己定义的类），外加特殊的 `SELF_TYPE`。

每个变量在**引入处必须声明类型**：let、case 分支、方法形参、属性。Cool 是**静态类型**语言，编译器靠这些声明做类型检查（doc 06）。

### 6.2 一致性/子类型（Conformance，记作 `≤`）

> 直觉：**子类的对象可以用在父类被要求的地方**（和 Java 的"向上转型"完全一样）。

- `C ≤ P` 当且仅当：`C` 继承自 `P`（直接或间接），或 `C = P`。
- `Object` 是根，所以任何类型 `A ≤ Object`。
- 例：若 `Cons inherits List`，则 `Cons ≤ List ≤ Object`，所以"期望 `List` 的地方"可以放 `Cons`。

### 6.3 SELF_TYPE——Cool 类型系统最精妙的一笔

`SELF_TYPE` 指"`self` 的**动态类型**"。用途：在会被继承的类里，避免写死返回类型。

```cool
class Silly {
  copy() : SELF_TYPE { self };      -- 返回类型随 self 动态变化
};
class Sally inherits Silly { };
class Main {
  x : Sally <- (new Sally).copy();  -- copy() 返回 SELF_TYPE = Sally
  main() : Sally { x };
};
```

关键规则：
- `SELF_TYPE_C ≤ P` 当且仅当 `C ≤ P`（`SELF_TYPE_C` 表示"类 C 里的 SELF_TYPE"）。
- `SELF_TYPE` **只能用在这 4 个地方**：`new SELF_TYPE`、方法返回类型、let 变量的声明类型、属性的声明类型。其他位置使用是错误。

### 6.4 void 与默认初始化（Default Initialization）

- **void**：类似 C 的 NULL / Java 的 null，是**所有类型的成员**。没有字面量写法；产生 void 的途径只有：给非 `Int/String/Bool` 的变量**不写初始化**（默认就是 void），或存 `while` 循环的结果（`while` 的值恒为 void）。
- **默认初始化表**：

| 类型 | 默认值 |
|---|---|
| `Int` | `0` |
| `String` | `""`（空串，**不是** void） |
| `Bool` | `false` |
| 其他类 | `void` |

- 对 void 做**分派（dispatch）或 case** → 运行时错误。

---

## 7. 表达式语义（逐类讲解：它运行时到底干什么）

> 这里给"直觉语义"；doc 06 讲怎么静态检查，doc 09 讲怎么编译成 LLVM IR。

### 7.1 赋值 `id <- e`
求值 `e`，把结果存入 `id`。类型要求：`e` 的类型必须 conform 到 `id` 的声明类型。整个表达式的值 = `e` 的值。

### 7.2 分派 Dispatch（面向对象的灵魂）
```
e0.f(e1, ..., en)       -- 动态分派：运行时看 e0 的真实类型，找该方法
e0@T.f(e1, ..., en)     -- 静态分派：强制用类 T 里的 f
f(e1, ..., en)          -- 等价 self.f(...)
```
求值顺序：先 `e0`，再从左到右 `e1...en`。**若 `e0` 是 void → 运行时错误**。动态分派保证了多态：`l.isNil()` 在 `l` 是 `Nil` 时调 `Nil.isNil`，是 `Cons` 时调 `Cons.isNil`。

### 7.3 条件 `if e1 then e2 else e3 fi`
`e1` 必须是 Bool。求值 `e1`，为真求 `e2`，为假求 `e3`。值 = 被选中分支的值。

### 7.4 循环 `while e1 loop e2 pool`
`e1` 必须是 Bool。反复：求 `e1`，假则结束（**循环值 = void**），真则求 `e2` 再回到判断。

### 7.5 块 `{ e1; e2; ...; en; }`
从左到右求值，**块的值 = 最后一个表达式 `en` 的值**（前面只是副作用）。

### 7.6 let 表达式
```cool
let x : T <- e1, y : T2 in body
```
- 引入局部变量，作用域 = `body`（及后面的初始化）。
- 有初始化则先求值再绑定；无初始化用默认值。
- 求值顺序：`e1` 绑定给 `x`，然后 `e2` 绑定给 `y`（**后面的能看到前面的**），最后求 `body`，值 = `body`。
- 同名后绑定**隐藏**前绑定。

### 7.7 case 表达式（按运行时类型分派）
```cool
case e0 of
  x1 : T1 => e1;
  x2 : T2 => e2;
  ...
esac
```
- 求值 `e0`，得动态类型 `D`。
- **从上到下**找第一个"声明类型是 `D` 的祖先（或等于 `D`）"的分支，把值绑给该分支变量 `xi`，求值 `ei`，值 = `ei`。
- 要求：各分支的声明类型**必须互不相同**；`e0` 为 void 或无匹配分支 → 运行时错误。

### 7.8 new
`new T` 分配一个 `T` 的对象，所有属性按默认值/初始化表达式初始化（**先父类后子类**、同层按源码顺序），值 = 该对象引用。

### 7.9 isvoid
`isvoid e`：`e` 是 void 则 `true`，否则 `false`。类型 Bool。

### 7.10 算术 `+ - * /` 与 `~`
- 操作数都必须是 Int，结果 Int。**整除**（`7/2 = 3`）。**除零 → 运行时错误**。
- `~e`：整数取负（补），操作数 Int，结果 Int。

### 7.11 比较 `< <= =`
- 类型规则：`Int↔Int`、`String↔String`、`Bool↔Bool` 必须**成对相同**；其他任意类型两两可比较（结果恒 Bool）。
- 语义：
  - `=`：Int 数值相等；String 内容相等；Bool 相等；**其他对象按"指针相等"**（是否同一对象）；`void = void` 为真，`void` 与任何非 void 为假。
  - `<`、`<=`：Int 数值序；String 字典序；Bool 按 `false < true`。

### 7.12 not
`not e`：`e` 必须 Bool，结果取反。

### 7.13 操作语义（Operational Semantics，形式化）

> CS143 有一讲专门讲"操作语义"——用**形式化规则**精确定义"表达式如何一步步求值"。这是把 §7 的直觉语义"数学化"，也是实现 codegen（doc 09）的最终依据。完整规则见 `refs/cool-manual-full.txt` 的 "Operational Semantics"，这里给核心思想。

**两个关键结构**：
- **环境（environment）E**：变量名 → 存储位置（location）。
- **存储（store）S**：位置 → 值。值记作 `X(a1=l1, ..., an=ln)`（类 X 的对象，属性 ai 存在位置 li）。

**小步（small-step）判断**：`⟨e, E, S⟩ → ⟨e', E', S'⟩`，读作"表达式 e 在环境 E、存储 S 下**一步**变成 e'，存储变 S'"。反复应用直到得到一个值。

**三条代表性规则**（直觉版）：

```
[Let]  ⟨e1,E,S⟩ → ⟨v1,S1⟩, l 是新位置
       ⟨let x:T <- e1 in e2, E, S⟩ → ⟨e2, E[x→l], S1[l→v1]⟩
       （先求初值，绑定到新位置，再求 body）

[Dispatch] 求值 e0 到 v0 = X(...)，查 X 的方法 f，形参换成实参
       （动态分派：按运行时类 X 找方法）

[Case]  求值 e0 到 v0 = X(...)，找第一个分支类型 T 使 X ≤ T，绑定分支变量，求值该分支
```

**为什么重要**：操作语义是"编译器实现正确与否"的**标准**——你写的 codegen 翻译出的机器行为，必须和这些规则一致。它是 PL 研究的入门工具（后面你会读到"类型系统 + 操作语义 = 语言的形式定义"）。


---

## 8. 内建类（Basic Classes）——方法签名速查

| 类 | 方法签名 | 说明 |
|---|---|---|
| `Object` | `abort() : Object` | 刷出输出后以 "abort\n" 中止 |
| | `type_name() : String` | 返回**动态类型**的类名 |
| | `copy() : SELF_TYPE` | **浅拷贝**（只复制自身，不递归复制指向的对象） |
| `IO` | `out_string(x:String) : SELF_TYPE` | 打印字符串，把 `\t`→tab、`\n`→换行，刷新，返回 self |
| | `out_int(x:Int) : SELF_TYPE` | 打印整数 |
| | `in_string() : String` | 读一行（不含换行），出错返回 `""` |
| | `in_int() : Int` | 读一个整数，出错返回 `0` |
| `String` | `length() : Int` | 长度 |
| | `concat(s:String) : String` | 拼接 self + s |
| | `substr(i:Int, l:Int) : String` | 从下标 i 起长 l 的子串（越界 → 运行时错误） |
| `Int` | （无专属方法） | 算术靠运算符 `+ - * / ~` 与比较 |
| `Bool` | （无专属方法） | 靠 `not` 与 `if/while` |

**硬性限制（typechecker 要检查）**：`Object`、`IO`、`Int`、`String`、`Bool` **不能被重新定义**；`Int`、`String`、`Bool` **不能被继承**。

---

## 9. 示例程序（`projects/coolc/tests/` 里会有这些）

### 9.1 阶乘（递归 + if + 算术）
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
输出：`120`

### 9.2 case 按类型分派
```cool
class Main inherits IO {
  describe(x : Object) : String {
    case x of
      i : Int    => "Int";
      s : String => "String";
      b : Bool   => "Bool";
      o : Object => "other";
    esac
  };
  main() : Object {
    out_string(describe(42).concat("\n"))
  };
};
```
输出：`Int`

### 9.3 动态分派 + 多态（链表求和）
```cool
class List {
  isNil() : Bool { true };
  head() : Int { 0 };
  tail() : List { self };
};
class Nil inherits List { };
class Cons inherits List {
  xcar : Int;
  xcdr : List;
  isNil() : Bool { false };
  head() : Int { xcar };
  tail() : List { xcdr };
  init(hd : Int, tl : List) : SELF_TYPE {
    { xcar <- hd; xcdr <- tl; self; }
  };
};
class Main inherits IO {
  sum(l : List) : Int {
    if l.isNil() then 0 else l.head() + sum(l.tail()) fi
  };
  main() : Object {
    let l : List <- (new Cons).init(3, (new Cons).init(2, new Nil)) in
      out_int(sum(l))
  };
};
```
输出：`5`。这里 `isNil/head/tail` 都是**动态分派**，`sum` 不用知道 `l` 到底是 `Nil` 还是 `Cons`。

### 9.4 SELF_TYPE（继承友好的返回类型）
```cool
class Silly {
  copy() : SELF_TYPE { self };
};
class Sally inherits Silly { };
class Main {
  x : Sally <- (new Sally).copy();
  main() : Sally { x };
};
```

---

## 10. Cool ↔ 真实语言对照（帮你迁移）

| Cool | 对应真实语言 | 备注 |
|---|---|---|
| `class C inherits P {}` | Java `class C extends P {}` | Cool 单继承 |
| `x <- e` | `x = e`（Java） | 别和 `=` 相等混淆 |
| `e.f(args)` | `e.f(args)` | 动态分派，语义一致 |
| `e@T.f(args)` | `T::f`（C++ 静态调用）/ `super.f`（近似） | Cool 显式静态分派 |
| `case ... of ... esac` | Java `switch`（类型版）/ 模式匹配（Scala `match`） | Cool 按运行时类型 |
| `let x:T <- e in body` | 局部变量声明 | Cool 一切皆表达式 |
| `SELF_TYPE` | Scala `this.type` | 精确返回类型 |
| `isvoid e` | `e == null` | |
| `{ e1; e2; }` | 语句块 | Cool 的块有值（最后一个） |

**一句话总结**：Cool ≈ **把 Java 压扁成"教学尺寸"**，保留静态类型 + 单继承 + 动态分派，去掉一切噪音。你吃透它，doc 02–09 就是"如何把这个小语言翻译成 LLVM IR"。

---

## 11. 本文你掌握了什么 / 易错点 / 下一步

**掌握**：Cool 的完整词法规则、语法 BNF、优先级表、类型系统（conformance/SELF_TYPE/void/默认初始化）、8 种表达式的语义、内建类方法、4 个示例程序。

**易错点（都是 lexer/parser/typechecker 的考点）**：
1. `=` 是相等，`<-` 是赋值，`<=` 是小于等于，`=>` 是 case 箭头——四个符号别混。
2. 关键字大小写不敏感，但 `true`/`false` 首字母必须小写。
3. 字符串只有 `\t` `\n` 两个转义，且 lexer **不翻译**它们；`\"` 不变成 `"`。
4. 块注释 `(* *)` 可嵌套。
5. 整数恒非负、上限 2147483647；字符串字面量上限 1024。
6. `Int/String/Bool` 不能继承不能重定义；`Object/IO` 不能重定义。

**下一步**：读 `02_词法分析.md`，写第一个组件 `lexer`，把这些词法规则变成可运行的 C++ 代码（CP1 里程碑）。
