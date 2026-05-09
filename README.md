# MemorySearch - Android/Linux 内存读写与搜索库

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Android%20%7C%20Linux-lightgrey)](https://developer.android.com/ndk)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

MemorySearch 是一个专为 **Android/Linux** 设计的轻量级 C++ 库，支持跨进程内存读写、特征码扫描、多线程搜索和结果集操作

## 特性
`` 需要与目标 相同的 UID 或者 已 Root 权限运行 ``
-  **跨进程内存读写**：内提供 `process_vm_readv`/`writev` 和 `/proc/pid/mem` 双方式，自动 fallback, 或 自实现 读写接口
- 🔍 **多种搜索模式**：
  - 精确值搜索（任意类型，如 `int`、`float`、自定义结构体）
  - 自定义谓词搜索（如范围、条件表达式）
  - UTF-8 / UTF-16 字符串搜索（大小写可选，含空终止符）
  - 十六进制特征码扫描（支持通配符 `??`）
  - 异步扫描（回调模式，可提前终止）
- ⚡ **多线程并行搜索**：自动划分内存区域，动态负载均衡，大幅提升扫描速度。
- 🧩 **内存类型智能分类**：基于 `/proc/pid/maps` 自动识别 17+ 种内存区域（堆、栈、代码段、匿名、Ashmem、Java 堆等）。
- 🔧 **结果集链式操作**：过滤、修改、刷新、回写一气呵成。
- 📝 **基础汇编注入**：通过 Keystone 引擎写入汇编指令（ARM/ARM64/x86/x86_64）。

## 快速开始

### 编译要求

- CMake 3.12+ 或直接使用 NDK 编译
- C++17 编译器（GCC 8+ / Clang 7+ / Android NDK r23+）
- **可选依赖**：[Keystone Engine](https://www.keystone-engine.org/)（若需 `write_assembly`，否则可禁用）

### 编译示例

#### Android NDK（可执行文件）
```bash
# 在项目根目录
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-24 \
      ..
make
```
## 1. 内存读写
```c++
Mem men(12345); // 输入pid 或者 进程名 默认构造 传入当前进程pid
 
// 读
//----------------

int value = mem.Read<int>(0x7DE58F6898); // 方法模版, 参数传入地址

struct Vec2 {
    float x,y;
}

Vec2 vec;
mem.read(0x7DE58F6898,&vec,sizeof(vec)); // 读结构体

// 写 
//----------------

mem.Write<int>(0x7DE58F6898,123) // 参数1 地址, 参数2 写入数值

mem.write(0x7DE58F6898,&vec,sizeof(vec)); // 写入结构体

std::vector<std::string> instructions = {
       "ADD	 W9, W9, #0x1",
       "NOP"
    };
mem.write_assembly(address, instructions); // 写人汇编指令

```
## 2. 内存搜索

### 内存类型
| 常量 | 含义 |
|------|------|
| `RANGE_C_HEAP` | C++ 堆内存 `Ch` |
| `RANGE_JAVA_HEAP` | Java 堆 `Jh` |
| `RANGE_C_ALLOC` | 分配器内存 `Ca` |
| `RANGE_C_DATA` | C 数据段 `Cd` |
| `RANGE_ANONYMOUS` | 匿名映射 `A` |
| `RANGE_STACK` | 栈 `S`|
| `RANGE_CODE_APP` | 应用代码段 `Xa` |
| `RANGE_CODE_SYSTEM` | 系统代码段 `Xs` |
| `RANGE_JAVA` | Java 虚拟机其他内存 `J` |
| `RANGE_ASHMEM` | Android 共享内存 `As`|
| `RANGE_VIDEO` | 视频/GPU 内存 `V` |
| `RANGE_B_BAD` | 坏内存区域 `B`|
| `RANGE_OTHER` | 其他 `O`|
| `RANGE_ALL` | 所有类型（默认） |

### 搜索参数 SearchParams

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `startAddress` | `uintptr_t` | `0` | 搜索起始地址（可裁剪） |
| `endAddress` | `uintptr_t` | `UINTPTR_MAX` | 搜索结束地址 |
| `memTypeMask` | `uint32_t` | `RANGE_ALL` | 内存类型掩码 |
| `align` | `bool` | `true` | 按类型大小对齐 |
| `parallel` | `bool` | `false` | 启用多线程搜索 |
| `numThreads` | `unsigned int` | `0` | 线程数（0 = 自动检测 CPU 核心数） |

### 结果集 `ResultSet<T>`

`ResultSet<T>` 是搜索结果的容器，封装了地址和值的列表，支持链式操作，方便对搜索结果进行二次过滤、修改和回写。

| 方法 | 说明 |
|------|------|
| `results()` / `size()` / `empty()` | 获取结果数组或数量 |
| `filter(pred)` | 返回新的过滤后结果集 |
| `filterSelf(pred)` | 原地过滤 |
| `modify(func)` | 原地修改每个结果的值（但不回写进程） |
| `refresh()` | 从进程重新读取每个地址的值 |
| `writeBack()` | 将所有结果的值写回进程 |
| `writeAll(newValue)` | 将所有地址的值设置为 `newValue` 并写回 |
| `writeOffset(offset, newValue)` | 对所有 `address+offset` 写入新值 |
| `addresses()` | 返回地址列表 |

### API

```cpp
// 按精确值搜索
ResultSet<T> find(const SearchParams& params, T value);

// 自定义条件搜索
ResultSet<T> find(const SearchParams& params,
                  std::function<bool(const SearchResult<T>&)> judge);

// 字符串搜索
std::vector<uintptr_t> findStringUTF8(const SearchParams& params,
                                      const std::string& str,
                                      bool includeNull = true,
                                      bool caseSensitive = true);
std::vector<uintptr_t> findStringUTF16(const SearchParams& params,
                                       const std::u16string& str,
                                       bool includeNull = true,
                                       bool caseSensitive = true);

// 特征码搜索（原始字节 + 掩码）
std::vector<uintptr_t> findPattern(const SearchParams& params,
                                   const std::vector<uint8_t>& pattern,
                                   const std::vector<uint8_t>& mask = {});

// 从字符串解析模式（如 "90 90 ?? 90"）
std::vector<uintptr_t> scanPattern(const SearchParams& params,
                                   const std::string& patternStr);

// 异步扫描（回调返回 false 时提前终止）
void scanPatternAsync(const SearchParams& params,
                      const std::string& patternStr,
                      std::function<bool(uintptr_t)> callback);
```
### 1. 按值精确搜索
```c++
    template <typename T>
    ResultSet<T> find(const SearchParams &params, T value)

    //使用
    Mem mem(pid);
    Search searcher(mem);

    Search::SearchParams params; // 搜索参数
    // 搜索 Ca 和 A 内存
    params.memTypeMask = MemType::RANGE_C_ALLOC | MemType::RANGE_ANONYMOUS; 
    params.align = true; // 内存对齐
    auto intResults = searcher.find<int>(params, 123456); // 精准搜索
    std::cout << "   找到 " << intResults.size() << " 个结果\n";
    if (!intResults.empty())
    {
        std::cout << "   前5个: ";
        for (size_t i = 0; i < std::min(5UL, intResults.size()); ++i)
        {
            std::cout << std::hex << "0x" << intResults.results()[i].address << " ";
        }
        std::cout << std::dec << "\n";
    }
```
### 2. 自定义条件搜索

```c++
  // 自定义条件搜索
    template <typename T>
    ResultSet<T> find(const SearchParams &params,std::function<bool(const SearchResult<T> &)> judge)

    // 2. 自定义条件搜索 (float 在 [3.14, 3.16] 内)
    std::cout << " 自定义条件搜索 float 在 [3.14, 3.16] 内";
    auto floatResults = searcher.find<float>(params, []( const Search::SearchResult<float> &res)
                                             { return res.value >= 3.14f && res.value <= 3.16f; });
    std::cout << "   找到 " << floatResults.size() << " 个结果\n";
    for (size_t i = 0; i < std::min(3UL, floatResults.size()); ++i)
    {
        std::cout << std::hex << "   0x" << floatResults.results()[i].address
                  << std::dec << " = " << floatResults.results()[i].value << "\n";
    }

    // 二次过滤
    floatResults.refresh(); // 刷新内存数值
            auto filtered = floatResults.filter([value](const auto &r)
                                                  { return r.value == value; }); // 过滤条件
    floatResults = std::move(filtered);

```

### 3. 字符串搜索
findStringUTF16  
findStringUTF8
```c++
//  UTF-8 字符串搜索 (区分大小写, 包含空终止符)
    auto utf8Addrs = searcher.findStringUTF8(params, "Hello", true, true);
    std::cout << "   找到 " << utf8Addrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(3UL, utf8Addrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << utf8Addrs[i] << std::dec << "\n";
    }

    //  UTF-16 字符串搜索
    auto utf16Addrs = searcher.findStringUTF16(params, u"World", true, true);
    std::cout << "   找到 " << utf16Addrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(3UL, utf16Addrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << utf16Addrs[i] << std::dec << "\n";
    }
```

### 4. 特征码搜索
scanPattern
```c++
 // 特征码扫描 (精确模式 "90 90 90 90")
    std::cout << "\n5. 特征码扫描 \"90 90 90 90\" (NOP 滑动):\n";
    auto patternAddrs = searcher.scanPattern(params, "90 90 90 90");
    std::cout << "   找到 " << patternAddrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(5UL, patternAddrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << patternAddrs[i] << std::dec << "\n";
    }

    // 带通配符的特征码扫描 "12 ?? 34"
    std::cout << "\n6. 带通配符 \"12 ?? 34\":\n";
    auto wildAddrs = searcher.scanPattern(params, "12 ?? 34");
    std::cout << "   找到 " << wildAddrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(5UL, wildAddrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << wildAddrs[i] << std::dec << "\n";
    }
    // 异步扫描 (找到前 3 个就停止)
    std::cout << "\n7. 异步扫描 \"?? ?? ?? ??\" 最多返回 3 个:\n";
    int count = 0;
    searcher.scanPatternAsync(params, "?? ?? ?? ??", [&count](uintptr_t addr)
                              {
        std::cout << std::hex << "   找到 0x" << addr << std::dec << "\n";
        return (++count < 3); });
    std::cout << "   异步扫描完成，共找到 " << count << " 个地址\n";
```

### 修改数值
```c++
  auto &vec = const_cast<std::vector<Search::SearchResult<int>>(currentResults.results());
vec[idx - 1].value = newVal;
  currentResults.modify([newVal](auto &r){ r.value = newVal; }).writeBack();
  currentResults.writeAll(12345);
  currentResults.writeOffset(0x24,12345);
```

### 5. 数值特征码搜索

```c++
   Mem mem(pid);
    Search search(mem);
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_C_ALLOC;
    auto results = search.find<int>(params, 313153600);
    std::cout << "找到 " << results.size() << " 个结果:" << std::endl;
    std::cout << "改善" << std::endl;
    results.filterSelf([&mem](const auto &res)
    { 
        return mem.Read<int>(res.address + 4) == -1275068293;
    }).filterSelf([&mem](const auto &res)
    {
        return mem.Read<int>(res.address + 12) == 0;
    });

    std::cout << "过滤后剩余 " << results.size() << " 个结果:" << std::endl;
    for (const auto &res : results.results())
    {
        std::cout << "地址: 0x" << std::hex << res.address
                  << " 值: " << std::dec << res.value << std::endl;
    }

    results.writeOffset(0x8,9999);
```
使用例子

```c++
#include "../core/Mem/Mem.hpp"
#include <iostream>
#include "../core/Mem/Search.hpp"

int main()
{
    uintptr_t address;
    int pid;
    std::cout << "请输入目标进程 PID: ";
    std::cin >> pid;
    std::cin.ignore(); // 忽略换行符
    std::cout << "请输入要写入的地址 (16 进制): ";
    std::cin >> std::hex >> address;
    Mem mem(pid);

    std::vector<std::string> instructions = {
       "ADD	 W9, W9, #0x1"
    };
    mem.write_assembly(address, instructions);

    // 313,153,600A;-1,275,068,293A;98A;0A
    Mem mem(pid);
    Search search(mem);
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_C_ALLOC;
    auto results = search.find<int>(params, 313153600);
    std::cout << "找到 " << results.size() << " 个结果:" << std::endl;
    std::cout << "改善" << std::endl;
    results.filterSelf([&mem](const auto &res)
    { 
        return mem.Read<int>(res.address + 4) == -1275068293;
    }).filterSelf([&mem](const auto &res)
    {
        return mem.Read<int>(res.address + 12) == 0;
    });

    std::cout << "过滤后剩余 " << results.size() << " 个结果:" << std::endl;
    for (const auto &res : results.results())
    {
        std::cout << "地址: 0x" << std::hex << res.address
                  << " 值: " << std::dec << res.value << std::endl;
    }

    results.writeOffset(0x8,9999);
    return 0;
}
```
## 多线程搜索
```c++
#include "../core/Mem/Mem.hpp"
#include <iostream>
#include "../core/Mem/Search.hpp"

int main()
{
    int pid;
    std::cout << "请输入目标进程 PID: ";
    std::cin >> pid;
    std::cin.ignore(); // 忽略换行符

    Mem mem(pid);
    Search search(mem);
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_ALL;
    params.parallel = true; // 启用多线程搜索

    int value;
    std::cout << "请输入要搜索的整数值: ";
    std::cin >> value;
    std::cin.ignore(); // 忽略换行符

    std::time_t startTime = std::time(nullptr);
    auto results = search.find<int>(params, value);
    std::cout << "找到 " << results.size() << " 个结果:" << std::endl;

    std::cout << "显示前 10 个结果:" << std::endl;
    for (int i = 0; i < 10 && i < results.size(); ++i)
    {
        const auto &res = results.results()[i];
        std::cout << "地址: 0x" << std::hex << res.address
                  << " 值: " << std::dec << res.value << std::endl;
    }
    std::time_t endTime = std::time(nullptr);
    std::cout << "搜索耗时: " << (endTime - startTime) << " 秒" << std::endl;

    params.parallel = false; // 切换回单线程搜索
    startTime = std::time(nullptr);
    auto singleThreadResults = search.find<int>(params, value);
    std::cout << "单线程搜索找到 " << singleThreadResults.size() << " 个结果:" << std::endl;

        std::cout << "显示前 10 个结果:" << std::endl;
    for (int i = 0; i < 10 && i < singleThreadResults.size(); ++i)
    {
        const auto &res = singleThreadResults.results()[i];
        std::cout << "地址: 0x" << std::hex << res.address
                  << " 值: " << std::dec << res.value << std::endl;
    }
    endTime = std::time(nullptr);
    std::cout << "单线程搜索耗时: " << (endTime - startTime) << " 秒" << std::endl;
}

```

```
请输入目标进程 PID: 17928
请输入要搜索的整数值: 1
找到 2180398 个结果:
显示前 10 个结果:
地址: 0x6aa0000c 值: 1
地址: 0x6aa003fc 值: 1
地址: 0x6aa004e8 值: 1
地址: 0x6aa00684 值: 1
地址: 0x6aa00780 值: 1
地址: 0x6aa0079c 值: 1
地址: 0x6aa0089c 值: 1
地址: 0x6aa00900 值: 1
地址: 0x6aa00914 值: 1
地址: 0x6aa00d88 值: 1
搜索耗时: 11 秒
单线程搜索找到 2181994 个结果:
显示前 10 个结果:
地址: 0x420005c 值: 1
地址: 0x4200110 值: 1
地址: 0x4200758 值: 1
地址: 0x4200760 值: 1
地址: 0x4200770 值: 1
地址: 0x4200888 值: 1
地址: 0x4200a88 值: 1
地址: 0x4200a90 值: 1
地址: 0x4200aa0 值: 1
地址: 0x4200c60 值: 1
单线程搜索耗时: 91 秒
```

`详细例子 查看` [main.cpp](./main.cpp)

```
D:\C++项目\MemorySearch>adb shell "su -c 'chmod 777 /data/local/tmp/MemorySearch && /data/local/tmp/MemorySearch'" 
输入进程名或 PID 25113

可用命令:
  search <type> <value>      首次搜索 (type: int, float)
  refine <type> <value>      在结果中再次搜索 (仅 int)
  list [N]                   显示前 N 个结果
  modify <index> <newvalue>  修改指定索引的结果
  modifyall <newvalue>       修改所有结果
  write                      将修改写回进程
  refresh                    从进程重新读取值
  pattern <hex>              特征码扫描, 如 "12 ?? 34"
  utf8 <string>              搜索 UTF-8 字符串
  utf16 <string>             搜索 UTF-16 字符串
  dump <addr> [size]         dump 内存
  clear                      清空当前结果集
  demo                       运行综合演示
  help                       显示本帮助
  exit                       退出

> demo

========== Search 综合演示 ==========

1. 搜索 int 值 123456 (全内存, 对齐):
   找到 11 个结果
   前5个: 0xab3d773c 0x7d8d20ad70 0x7de4e4c510 0x7de78e65fc 0x7e7b18b7b4 

2. 自定义条件搜索 float 在 [3.14, 3.16] 内:
   找到 369 个结果
   0xa8a08980 = 3.15252
   0xa8f58eac = 3.14055
   0xa8f58eb4 = 3.14522

3. UTF-8 字符串搜索 "Hello" (区分大小写, 含 '\0'):
   找到 2 个地址
   0x7de71d1645
   0x7decd6ce4c

4. UTF-16 字符串搜索 u"World" (小端序, 含空终止符):
   找到 2 个地址
   0x7d95f20c66
   0x7da1520c66

5. 特征码扫描 "90 90 90 90" (NOP 滑动):
   找到 487 个地址
   0x7d7cb26896
   0x7d7cb26897
   0x7d7cb26898
   0x7d7cb26899
   0x7d7cb2689a

6. 带通配符 "12 ?? 34":
   找到 5935 个地址
   0x42b082f5
   0x42b084ad
   0xa8afdd84
   0xa8edf1e2
   0xa8ee9a22

7. 异步扫描 "?? ?? ?? ??" 最多返回 3 个:
   找到 0x4200000
   找到 0x4200001
   找到 0x4200002
   异步扫描完成，共找到 3 个地址

========== 演示结束 ==========

> 

```
