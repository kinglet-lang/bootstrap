# C++ 废弃/拒绝提案 → Kinglet 设计灵感

> C++ 委员会因为向后兼容、实现复杂度、ABI 稳定性等原因拒绝或长期搁置的提案，
> 在一个无历史包袱的新语言中完全可以采纳。

---

## 一、C++ 正式废弃/移除的特性

### 1.1 C++11 废弃

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `auto_ptr` | 语义混乱（移动语义的前身，拷贝=转移） | Kinglet：独占所有权用 `own<T>`，引用用 `&T`，值语义默认 |
| `register` 关键字 | 编译器忽略它几十年 | Kinglet：不需要，编译器优化 |
| `bool b++;` (increment on bool) | 无意义 | Kinglet：bool 不可算术运算 |
| C 风格强转换 `(int)x` | 不安全 | Kinglet：用 `x as int` 或 `cast<int>(x)` 显式转换 |
| 三字符组 `??=` → `#` | 没人用 | Kinglet：不存在 |

### 1.2 C++14 废弃

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `std::strstream` | 被 stringstream 取代 | Kinglet：用 `{}` 格式化，不用流 |
| `std::auto_ptr` 再次强调废弃 | 移动语义已完善 | 同上 |

### 1.3 C++17 移除/废弃

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `std::auto_ptr` **正式移除** | - | - |
| `std::bind1st/bind2nd` | 被 lambda 完全取代 | Kinglet：lambda 是一等公民 |
| `std::ptr_fun/mem_fun` | 同上 | 同上 |
| `std::mem_fun_ref` | 同上 | 同上 |
| `std::pointer_to_unary_function` | 同上 | 同上 |
| `std::raw_storage_iterator` | 不安全 | Kinglet：不需要手动 placement new |
| 三字符组 **正式移除** | - | - |
| `std::iterator` (基类) | 被迭代器 concept 取代 | Kinglet：用 trait/concept 约束 |
| `static_assert` 无消息 (C++17 允许省略) | - | Kinglet：`static_assert(cond)` 一向不需要消息 |

### 1.4 C++20 废弃

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `std::shared_ptr::unique()` | 在多线程下不可靠 | Kinglet：所有权模型更清晰 |
| `std::allocator<T>::is_always_equal` | 设计有问题 | Kinglet：allocator 简化设计 |
| `std::weak_ordering` 部分比较 | 复杂 | Kinglet：`<=>` 直接返回 ordering enum |
| `volatile` 的大部分操作 | 语义不清晰 | Kinglet：用 `@atomic` 或显式内存模型 |
| `std::to_string` (浮点) | 精度问题 | Kinglet：用 `{}` 格式化 |

### 1.5 C++23 废弃

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `std::aligned_storage` / `std::aligned_storage_t` | 应该用 `alignas` | Kinglet：`alignas` 关键字 |
| `std::type_identity` 某些用法 | 被 deducing this 取代 | - |
| `std::move_iterator` 的 `operator->` | 危险 | Kinglet：迭代器设计简化 |

### 1.6 C++26 预期

| 特性 | 原因 | Kinglet 启示 |
|------|------|-------------|
| `std::vector<bool>` 可能被废弃/重新设计 | 不满足容器要求 | Kinglet：`BitArray` 独立类型 |
| 更多 C API 封装被标记废弃 | 现代替代品已就绪 | Kinglet：标准库从零设计 |

---

## 二、长期被拒绝/搁置的重大提案

### 2.1 模式匹配 (Pattern Matching) — P2688 / P1371

**提案内容：**
```cpp
// P2688 pattern matching (C++26 可能性)
auto result = inspect(x) {
    0       => "zero",
    [a, b]  => a + b,
    Point{x, y} if x > 0 => "right",
    _       => "other"
};
```

**C++ 现状：** 从 P1371 (2019) 到 P2688 (2024)，经过 5+ 年仍未进入标准。
争议点：`inspect` vs `match` 关键字、exhaustiveness checking、是否需要 ADT。

**Kinglet 优势：**
- ✅ 已经实现 `inspect` 作为一级控制流
- 可以直接走 Rust/Zig 路线：destructuring + exhaustiveness
- 不需要 `switch` 的 fall-through 遗留语义

**Kinglet 应该做的：**
```rust
// 比 C++ 提案更进一步
match (value) {
    0 => "zero",
    1..10 => "small",                    // 范围模式
    [a, b, ..rest] => process(rest),     // 切片模式
    Point { x, y } if x > 0 => "right", // 结构体解构 + guard
    Some(v) => v,                        // enum 解构
    _ => "other"
}
```

### 2.2 Contracts (契约) — P2900

**提案内容：**
```cpp
int f(int x)
    [[pre: x > 0]]
    [[post res: res > 0]]
{
    return x * 2;
}
```

**C++ 现状：** 从 C++20 就计划纳入，反复撤回（ABI 问题、语义争议），C++26 再度尝试。
核心争议：违反契约是 `terminate` 还是 `throw`？violations 是否可观察？

**Kinglet 优势：**
- 无 ABI 稳定性要求
- 可以定义清晰的语义：pre 违反 = panic，post 违反 = panic
- 可以做编译时检查（配合 const/type system）

**Kinglet 应该做的：**
```rust
fn divide(a: int, b: int) -> int
    requires b != 0           // precondition
    ensures result * b == a   // postcondition (可选)
{
    return a / b;
}

fn sort(arr: []int)
    ensures is_sorted(arr)    // postcondition
{
    // ...
}
```

### 2.3 静态反射 (Static Reflection) — P2996

**提案内容：**
```cpp
// P2996 反射 (C++26 候选)
constexpr auto r = ^MyStruct;
template for (constexpr auto member : std::meta::members_of(r)) {
    std::print("{}", std::meta::name_of(member));
}
```

**C++ 现状：** P2996 在 C++26 中进展顺利但仍有大量未解决问题（value-based vs type-based reflection）。

**Kinglet 优势：**
- 编译到字节码 VM，可以在 VM 层面原生支持反射
- 类型信息可以在编译时保留到运行时（不需要 `typeid` hack）

**Kinglet 应该做的：**
```rust
struct User {
    name: string,
    age: int,
}

// 原生反射（VM 内置）
reflect User       // 返回类型描述符
    .fields()      // [{name: "name", type: string}, ...]
    .field("age")  // FieldDescriptor
```

### 2.4 元类 (Metaclasses) — P0707 / P0708 (Herb Sutter)

**提案内容：**
```cpp
struct(value) Point {  // metaclass 'value' 自动生成
    int x, y;          // 比较、hash、格式化...
};
```

**C++ 现状：** Herb Sutter 提出后无实质性推进。委员会认为过于激进，影响编译时间。

**Kinglet 优势：**
- 新语言无编译时间包袱
- `struct` 天然可以是 value-semantic
- 可以用 `derive` 或 annotation 自动实现 trait

**Kinglet 应该做的：**
```rust
@derive(Hash, Eq, Format)    // annotation
struct Point {
    x: int,
    y: int,
}
// 编译器自动生成 Hash、Eq、Format 实现
```

### 2.5 管道操作符 (Pipeline Operator) — P2011 / P2672

**提案内容：**
```cpp
auto result = x |> f |> g(y);  // 等价于 g(f(x), y)
```

**C++ 现状：** 多次提案均被拒绝。原因：与现有运算符优先级冲突、重载语义不清。

**Kinglet 优势：**
- 可以定义清晰的优先级
- 不需要与 `<<` 重载竞争

**Kinglet 应该做的：**
```rust
let result = data
    |> transform
    |> filter(is_valid)
    |> collect;
```

### 2.6 统一函数调用 (Unified Function Call) — N4165 / P1478

**提案内容：**
```cpp
// 让 f(x) 和 x.f() 等价
x.size()  // 或
size(x)   // 统一调用
```

**C++ 现状：** 被拒绝，因为会破坏已有代码的 ADL 查找。

**Kinglet 优势：**
- 新语言可以统一
- Rust 的方法调用 + trait 已经证明这条路可行

**Kinglet 应该做的：**
```rust
// 自动 UFCS：自由函数可以像方法一样调用
fn length(s: string) -> int { ... }

"hello".length()  // 等价于 length("hello")
```

### 2.7 Deducing `this` — P0847 (C++23 ✅ 已接受！但... )

**提案内容：**
```cpp
struct Foo {
    void f(this Foo const& self);  // 显式 this 参数
};
```

**C++ 现状：** C++23 已接受，但实现缓慢（MSVC/gcc/clang 都未完全实现）。

**Kinglet 优势：**
- 从第一天就支持 `self` 显式参数
- 不需要 const/mutable 重复

**Kinglet 应该做的：**
```rust
impl Display for Point {
    fn fmt(self) -> string {  // self 显式
        return "({}, {})" % (self.x, self.y);
    }
}
```

### 2.8 `std::expected<T, E>` — P0323 (C++23 ✅)

**已经进入 C++23**，但 C++ 的实现受限于无 sum type 支持。

**Kinglet 应该做的：**
```rust
// enum 作为真正的 sum type
enum Result<T, E> {
    Ok(T),
    Err(E),
}

fn read_file(path: string) -> Result<string, IoError> {
    // ...
}
```

### 2.9 协程改进 — 多个提案

**C++ 现状：** C++20 协程是最小化设计，大量关键组件留给了库实现。
`std::generator` (C++23) 才补上最基础的。`std::task` 仍未标准化。

**Kinglet 优势：**
- 可以从零设计 async/await 语义
- VM 层面原生支持 suspend/resume

**Kinglet 应该做的：**
```rust
async fn fetch(url: string) -> string {
    let response = await http_get(url);
    return response.body;
}

// 或 generator
fn fibonacci() -> Generator<int> {
    mut a = 0, b = 1;
    loop {
        yield a;
        (a, b) = (b, a + b);
    }
}
```

### 2.10 `string_interpolation` — P2091

**提案内容：**
```cpp
auto s = std::format("{} is {}", name, age);  // C++20 已有
auto s = f"{name} is {age}";                  // 提议的插值语法
```

**C++ 现状：** `std::format` (C++20) 已标准化，但编译时格式化检查 (`std::print`, `std::println`) C++23 才有。
字符串插值语法多次被拒绝。

**Kinglet 优势：**
- 可以直接用 `"{}"` 格式化（io-system.md 已设计）
- 可以考虑字符串插值 `"{name}"` 语法

**Kinglet 应该做的：**
```rust
io::out("{} is {} years old", name, age);  // 已设计
// 或未来
io::out(f"{name} is {age} years old");     // 编译时检查
```

---

## 三、C++ 委员会讨论但未推进的特性

### 3.1 Defaultable / Deleted 之外的"特殊成员函数控制"

```cpp
// C++ 只能 =default 或 =delete
struct Foo {
    Foo(Foo&&) = default;
    Foo(const Foo&) = delete;
};
```

**Kinglet 可以做：**
```rust
struct Foo {
    // 默认所有值语义行为
    // 用 annotation 控制
    @no_copy     // 禁止拷贝
    @no_move     // 禁止移动
}
```

### 3.2 Named Arguments (命名参数) — 多次提案被拒

```cpp
// 提议但被拒
create_window(width=800, height=600, title="Hello");
```

**C++ 现状：** 反复被拒，因为与函数重载、模板交互太复杂。

**Kinglet 应该做的：**
```rust
// Builder 模式或直接命名参数
create_window(width: 800, height: 600, title: "Hello");
```

### 3.3 枚举改进 (Smart Enums / Algebraic Data Types)

```cpp
// C++ proposal: 关联数据的 enum
enum class Shape {
    Circle(double radius),
    Rectangle(double w, double h),
};
```

**C++ 现状：** 反复提案，每次都被搁置。C++ 的 enum class 太弱。

**Kinglet 应该做的：**
```rust
enum Shape {
    Circle(radius: float),
    Rectangle(width: float, height: float),
}

fn area(s: Shape) -> float {
    match (s) {
        Circle(r) => 3.14159 * r * r,
        Rectangle(w, h) => w * h,
    }
}
```

### 3.4 范围 for 的解构 (Structured Bindings 增强)

**C++17** 已有 `auto [x, y] = point;`，但：
- 不能在函数参数中使用
- 不能自定义绑定规则（直到 C++26 才部分改进）
- 与 pattern matching 不统一

**Kinglet 应该做的：**
```rust
// 统一的解构
let (x, y) = point;
let Point { x, y } = point;
let [first, ..rest] = array;

// 函数参数中也可解构
fn distance((x1, y1): Point, (x2, y2): Point) -> float {
    // ...
}
```

### 3.5 Immovable Types (不可移动类型)

**C++ 现状：** `std::mutex`, `std::atomic` 等不可移动，导致很多限制。

**Kinglet 应该做的：**
```rust
@ pinned
struct Mutex {
    // 编译器保证：永远不移动内存地址
}
```

### 3.6 Type-safe `printf` — N3735 (多次变体被拒)

**C++ 现状：** `printf` 不安全，`std::format` (C++20) 终于替代。但 `std::print` C++23 才有。

**Kinglet：** 已在 io-system.md 中设计。`io::out("{}", x)` 在编译时检查类型。

---

## 四、Kinglet 独特机会：C++ 不可能做到的事

### 4.1 NaN-boxing 值表示

C++ 无法改变值表示。Kinglet VM 可以用 NaN-boxing 统一 `int/float/bool/null/pointer` 为 64-bit。

### 4.2 原生 UTF-8 字符串

C++ 的 `char` / `wchar_t` / `char8_t` / `char16_t` / `char32_t` 是历史灾难。
Kinglet：**字符串就是 UTF-8，句号。**

### 4.3 无头文件 / 模块系统

C++20 Modules 实现缓慢，大部分项目仍用 `#include`。
Kinglet：`using` 引入模块，从第一天就是模块化的。

### 4.4 错误处理不用异常

C++ 异常 ABI 不兼容、性能不确定。
Kinglet：`Result<T, E>` + `match`，零开销错误处理。

### 4.5 去掉未定义行为 (UB)

C++ 有 200+ 种 UB。Kinglet：VM 层面定义所有行为（可能 panic，但不会"未定义"）。

---

## 五、按实现优先级排序的 Kinglet 特性路线图

### 🔥 P2 (已在计划中)
| 特性 | C++ 对应 | 难度 |
|------|---------|------|
| `enum` (ADT / smart enum) | enum class (被拒增强) | ★★★ |
| `struct` + 解构 | structured bindings | ★★★ |
| Pattern matching 增强 | P2688 (搁置) | ★★☆ |

### 🔥 P3
| 特性 | C++ 对应 | 难度 |
|------|---------|------|
| `trait` (概念) | concepts (C++20 已有，但受限) | ★★★★ |
| Module system | C++20 Modules (实现缓慢) | ★★★ |
| `Result<T, E>` | `std::expected` (C++23) | ★★☆ |
| `@derive` annotation | Metaclasses P0707 (搁置) | ★★★ |

### 🔥 P4+
| 特性 | C++ 对应 | 难度 |
|------|---------|------|
| async/await | C++20 Coroutines (最小化) | ★★★★★ |
| Reflection | P2996 (进行中) | ★★★★ |
| Contracts | P2900 (反复撤回) | ★★★ |
| UFCS | N4165 (被拒) | ★★★ |
| Pipeline operator | P2011 (被拒) | ★★☆ |
| Named arguments | 多次被拒 | ★★☆ |
| String interpolation | P2091 (被拒) | ★★☆ |

---

## 六、Kinglet 与 C++ 提案的关系总结

| Kinglet 特性 | C++ 提案编号 | C++ 状态 | Kinglet 可行性 |
|-------------|-------------|---------|-------------|
| `inspect`/`match` | P1371 → P2688 | ⏳ 5年+ 仍在讨论 | ✅ 已实现基础版 |
| Contracts | P2900 | ❌ 反复撤回 | ✅ 可做 |
| Reflection | P2996 | ⏳ C++26 可能 | ✅ VM 层面天然支持 |
| Metaclasses | P0707/P0708 | ❌ 搁置 | ✅ `@derive` |
| ADT enum | 多次提案 | ❌ 反复被拒 | ✅ 核心特性 |
| UFCS | N4165 | ❌ 被拒 | ✅ 可做 |
| Pipeline `|>` | P2011 | ❌ 被拒 | ✅ 优先级清晰 |
| Named args | 多次 | ❌ 被拒 | ✅ 可做 |
| String interpolation | P2091 | ❌ 被拒 | ✅ 编译时检查 |
| `async`/`await` | 多个 | ⏳ 最小化协程已入 | ✅ VM 原生支持 |
| 格式化 I/O | `std::format` | ✅ C++20 | ✅ 已设计 |
| Error handling | `std::expected` | ✅ C++23 | ✅ enum + match |

---

*"C++ 委员会的遗憾，就是 Kinglet 的机会。"*
