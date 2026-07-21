# MemorySearch — Android/Linux 内存读写与搜索库

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Android%20%7C%20Linux-lightgrey)](https://developer.android.com/ndk)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![ARM64](https://img.shields.io/badge/arch-ARM64%20%7C%20x86__64-blue)]()

专为 **Android/Linux** 设计的轻量级 C++ 跨进程内存库。支持零拷贝读写、多线程搜索、模糊搜索和结果集操作。

> ⚠️ 需要 root 权限或与目标进程相同 UID。

---

## 架构

```
MemBase (抽象接口)
├── Mem         — 传统读写 (process_vm_readv/preload64)
└── MemMmap     — 零拷贝读写 (mmap /proc/pid/mem)

ProcIO          — 共享 I/O 基础设施 (fd管理/pvm/pread)

SearchEngine    — 多线程搜索引擎 (BMH + 并行扫描)
    ├── search<T>()          精确值搜索
    ├── searchCompare<T>()   模糊搜索 (GT/LT/NEQ/GTE/LTE)
    ├── searchRange<T>()     范围搜索
    ├── searchString()       UTF-8/UTF-16 字符串
    └── searchPattern()      特征码 + 通配符

ResultSet<T>    — 结果集 (链式操作: filter/refresh/modify/writeBack/page)

FuzzySearch     — 模糊搜索工作流 (未知值 → 快照 → 精炼)
    ├── BulkResults<T>       紧凑存储 (支持 1亿+ 条目)
    └── SnapshotStore        快照比较 (区域/个体 双模式)

FastSearch      — Boyer-Moore-Horspool 跳表优化
```

---

## 性能

> 测试环境: Android ARM64, 8核, com.gameplier.kontra, ~4.9GB 可搜索内存

| 操作 | 耗时 | 吞吐 |
|------|------|------|
| 搜索 int=123456 (4.9GB, 32MB块) | **1.4s** | 3,449 MB/s |
| 搜索 int=100 (3.8GB RW) | 5.4s | 715 MB/s |
| 搜索 UTF-8 "Player" | 1.2s | 4,504 MB/s |
| 搜索 ARM64 NOP (420MB) | 342ms | 1,304 MB/s |

| MemMmap vs Mem | Mem | MemMmap | 倍数 |
|----------------|-----|---------|------|
| 单次 Read\<int\> | ~850ns | **~4ns** | **214x** |
| 批量随机读×100K | 181ms | **3.5ms** | **52x** |
| 指针链跳转×50K | 120ms | **3.7ms** | **32x** |
| 写入×10K | 46ms | **21ms** | **2.2x** |

---

## 快速开始

### 编译

```bash
# Android NDK (CMake)
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 ..
make

# 或 ndk-build
ndk-build
```

### 部署

```bash
adb push out/build/ndk-arm64/MemorySearch /data/local/tmp
adb shell "su -c 'chmod 777 /data/local/tmp/MemorySearch && /data/local/tmp/MemorySearch'"
```

---

## API 参考

### 1. 内存读写

```cpp
#include "core/Mem/Mem.hpp"
#include "core/Mem/MemMmap.hpp"

// ── Mem: 传统读写 (适合偶尔使用) ──
Mem mem(12345);                        // PID 或进程名
int value = mem.Read<int>(0x7abc);     // 读 int
float f = mem.Read<float>(0x7def);     // 读 float
mem.Write<int>(0x7abc, 999);           // 写 int

// 读结构体
struct Vec2 { float x, y; };
Vec2 vec;
mem.read(0x7abc, &vec, sizeof(vec));

// 汇编注入 (需 Keystone)
mem.write_assembly(addr, {"ADD W9, W9, #1", "NOP"});

// ── MemMmap: 零拷贝读写 (适合反复操作) ──
MemMmap mmap(pid);
mmap.mapRegions(MemType::RANGE_RW);     // 一次性映射可读写区
int v = mmap.Read<int>(0x7abc);         // 映射内=memcpy (零syscall)
void* ptr = mmap.getPtr(0x7abc);        // 零拷贝指针
*(int*)ptr = 999;                       // 直接写入目标进程内存!
```

### 2. 搜索

```cpp
#include "core/Mem/Search.hpp"

SearchEngine search(mem);    // 传 Mem 或 MemMmap 均可
SearchParams p;
p.memTypeMask = MemType::RANGE_RW;   // 搜索可读写区域
p.maxResults  = 50000;               // 结果上限 (防OOM)
p.chunkSize   = 32*1024*1024;        // 32MB 块

// ── 精确值搜索 ──
auto r = search.search<int>(p, 100);
printf("%zu 个结果\n", r.size());

// ── 模糊搜索 ──
auto gt = search.searchCompare<int>(p, 999, CompareOp::GT);   // > 999
auto lt = search.searchCompare<int>(p, 0,   CompareOp::LT);   // < 0
auto ne = search.searchCompare<int>(p, 0,   CompareOp::NEQ);  // != 0

// ── 范围搜索 ──
auto rf = search.searchRange<float>(p, 3.14f, 3.16f);

// ── 字符串搜索 ──
auto u8  = search.searchString(p, "Unity", true, true);       // UTF-8
auto u16 = search.searchStringUTF16(p, u"World", true, true); // UTF-16

// ── 特征码扫描 ──
auto pat = search.searchPattern(p, {0x1F,0x20,0x03,0xD5});   // ARM64 NOP
auto hex = search.scanPatternString(p, "12 ?? 34");           // 通配符

// ── 异步扫描 (可提前终止) ──
search.searchPatternAsync(p, "90 90 90 90", [](uintptr_t addr) {
    printf("找到: 0x%llx\n", addr);
    return true;  // false = 停止
});

// ── 进度回调 + 取消 ──
auto r2 = search.search<int>(p, 100, [](double progress) {
    printf("进度: %.0f%%\n", progress * 100);
    return progress < 0.5;  // 50% 时取消
});
```

### 3. 结果集操作

```cpp
ResultSet<int> results = search.search<int>(p, 100);

// 刷新 (从进程重新读取值)
results.refresh();

// 过滤
auto f1 = results.filter([](auto& r) { return r.value > 50; });
results.filterSelf([](auto& r) { return r.address > 0x7000000000; });

// 偏移过滤 (检查 address+offset 处的值)
auto f2 = results.filterOffset<int>(4, CompareOp::EQ, -12345);

// 修改 + 写回
results.modify([](auto& r) { r.value = 999; });
results.writeBack();                    // 全部写回
results.writeAll(0);                    // 全部设为0
results.writeOffset(8, 999);           // address+8 处写999

// 分页
auto page = results.page(0, 20);       // 第0页, 每页20条

// 集合操作
auto inter = results.intersect(other);
auto uni   = results.unite(other);

// 导出地址
auto addrs = results.addresses();
for (auto a : addrs) printf("0x%llx\n", a);
```

### 4. 模糊搜索工作流 (游戏修改)

```cpp
#include "core/Mem/FuzzySearch.hpp"

// 经典工作流: 未知值 → 快照 → 改变 → 精炼
MemMmap mmap(pid);
mmap.mapRegions(MemType::RANGE_RW);
FuzzySearch fuzzy(mmap);
SearchParams sp; sp.memTypeMask = MemType::RANGE_RW;

// 1. 未知值搜索
fuzzy.searchUnknown<int32_t>(sp);         // 自动选区域快照/个体模式

// 2. 在游戏中改变目标值, 然后精炼
fuzzy.refine<int32_t>(CompareOp::INCREASED);  // 变大
fuzzy.refine<int32_t>(CompareOp::CHANGED);    // 变化

// 3. 精确值过滤
fuzzy.filterExact<int32_t>(12345);

// 4. 修改 + 写回
for (size_t i = 0; i < fuzzy.size(); i++)
    fuzzy.setValueAt<int32_t>(i, 99999);
fuzzy.writeBack<int32_t>();
```

### 5. 内存类型常量

| 常量 | 含义 | 典型大小 |
|------|------|---------|
| `RANGE_ALL` | 全部可读内存 | ~5 GB |
| `RANGE_RW` | 全部可读写 | ~4 GB |
| `RANGE_C_HEAP` | C++ 堆 | — |
| `RANGE_JAVA_HEAP` | Java 堆 | ~3 GB |
| `RANGE_C_ALLOC` | 分配器内存 | ~170 MB |
| `RANGE_ANONYMOUS` | 匿名映射 | ~400 MB |
| `RANGE_STACK` | 栈 | ~8 MB |
| `RANGE_CODE_APP` | 应用代码 | ~420 MB |
| `RANGE_FILE_DATA` | 文件映射数据 | ~400 MB |
| `RANGE_CODE_SYSTEM` | 系统代码 | ~165 MB |

### 6. CompareOp 运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `EQ` | == | `searchCompare<int>(p, 100, EQ)` |
| `NEQ` | != | `searchCompare<int>(p, 0, NEQ)` |
| `GT` / `GTE` | > / >= | `searchCompare<int>(p, 999, GT)` |
| `LT` / `LTE` | < / <= | `searchCompare<int>(p, -999, LT)` |
| `RANGE` | min ≤ x ≤ max | `searchRange<float>(p, 0.9f, 1.1f)` |
| `CHANGED` | 值改变 | (需 FuzzySearch + 快照) |
| `UNCHANGED` | 值未变 | (需 FuzzySearch + 快照) |
| `INCREASED` | 值变大 | (需 FuzzySearch + 快照) |
| `DECREASED` | 值变小 | (需 FuzzySearch + 快照) |

---

## 交互式工具

### main.cpp — 搜索控制台

```bash
adb shell "su -c /data/local/tmp/MemorySearch"
```

```
> search int 100        # 搜索 int=100
> fuzzy int gt 999      # 搜索 int>999
> range float 3.14 3.16 # 范围搜索
> list 10               # 显示前10个结果
> modify 1 99999        # 修改第1个结果为99999
> write                 # 写回进程
> pattern "12 ?? 34"    # 特征码扫描
> utf8 "Unity"          # UTF-8 字符串搜索
```

### fuzzy_tool — 模糊搜索工具

专为游戏内存修改设计的完整工具链:

```bash
adb shell "su -c /data/local/tmp/fuzzy_tool"
```

```
r 6           ← 选 ANONYMOUS 区域 (400MB, 适中)
t i32         ← 选 int32 类型
u             ← 未知值搜索 (自动快照)
(游戏中改变数值)
f +           ← 精炼: 变大 | f - 变小 | f ~ 变化
l             ← 列出结果
ma 99999      ← 修改全部
w             ← 写回!
lock 99999    ← 锁定 (后台每500ms写入)
```

---

## 选择指南

| 场景 | 推荐 | 原因 |
|------|------|------|
| 一次性读取几个地址 | `Mem` | 即开即用 |
| 反复搜索同一进程 | `MemMmap` | 映射后零syscall |
| 模糊精炼 (refresh/filterOffset) | `MemMmap` | 百万次读=百万次memcpy |
| 指针链跳转 | `MemMmap` | getPtr() 零拷贝 |
| 单次特征码扫描 | `Mem` | 无需等待映射 |
| 低内存设备 (<4GB) | `Mem` | MemMmap 需 GB 级 RAM |
| 搜索后需要过滤精炼 | `SearchEngine + MemMmap` | 透明替换 |
| 未知值→精炼工作流 | `FuzzySearch + MemMmap` | 最优 |

---

## 文件结构

```
core/Mem/
├── Membase.hpp/cpp    — 抽象基类 (read/write 接口 + maps 查询)
├── ProcIO.hpp/cpp      — 共享 I/O (fd管理 / process_vm / pread)
├── ProcMap.hpp         — 内存映射解析 + 类型分类
├── Process.hpp/cpp     — 进程工具 (查找PID, 列举进程)
├── Mem.hpp/cpp         — 传统读写 (syscall)
├── MemMmap.hpp/cpp     — 零拷贝读写 (mmap)
├── Search.hpp/cpp      — 搜索引擎 (BMH + 并行扫描)
├── FastSearch.hpp      — BMH 跳表算法
├── FuzzySearch.hpp     — 模糊搜索 (BulkResults + SnapshotStore)
└── Keystone/           — 汇编引擎 (可选)

test/
├── benchmark.cpp       — 自测套件 (23项)
├── fuzzy_tool.cpp      — 游戏内存修改工具
├── mmap_bench.cpp      — Mem vs MemMmap 对比
├── test1.cpp           — 字符串搜索 + 汇编注入
└── test2.cpp           — 异步模式扫描

main.cpp                — 交互式搜索控制台
```

---

## 依赖

- C++17 编译器 (GCC 8+ / Clang 7+ / NDK r23+)
- CMake 3.12+
- [Keystone Engine](https://www.keystone-engine.org/) (可选, 仅 `write_assembly` 需要)

## License

MIT © 2026 Aboy-g
