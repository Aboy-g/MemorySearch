# libMem Android/Linux 内存读写搜索库
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
支持多种搜索方式

分类内存
```c++
namespace MemType
{
    constexpr uint32_t RANGE_ALL        = 0;            // 所有内存区域 (all)
    constexpr uint32_t RANGE_C_HEAP     = 1 << 0;       // C++ 堆内存 (ch)
    constexpr uint32_t RANGE_JAVA_HEAP  = 1 << 1;       // Java 虚拟机堆内存 (jh)
    constexpr uint32_t RANGE_C_ALLOC    = 1 << 2;       // C++ 分配器内存 (ca)
    constexpr uint32_t RANGE_C_DATA     = 1 << 3;       // C++ 数据段 (cd)
    constexpr uint32_t RANGE_C_BSS      = 1 << 4;       // C++ BSS 段 (cb)
    constexpr uint32_t RANGE_ANONYMOUS  = 1 << 5;       // 匿名内存区域 (a)
    constexpr uint32_t RANGE_STACK      = 1 << 6;       // 栈内存区域 (s)
    constexpr uint32_t RANGE_CODE_APP   = 1 << 14;      // 应用程序代码段 (xa)
    constexpr uint32_t RANGE_CODE_SYSTEM= 1 << 15;      // 系统代码段 (xs)
    constexpr uint32_t RANGE_JAVA       = 1 << 16;      // Java 虚拟机内存 (j)
    constexpr uint32_t RANGE_B_BAD      = 1 << 17;      // 坏内存区域 (b)
    constexpr uint32_t RANGE_ASHMEM     = 1 << 19;      // Android 共享内存 (as)
    constexpr uint32_t RANGE_VIDEO      = 1 << 20;      // 视频内存区域 (v)
    constexpr uint32_t RANGE_OTHER      = 1 << 31;      // 其他内存区域 (o)
    constexpr uint32_t RANGE_NULL_PAGE  = 99999;        // 空页或无效内存 (null)
}
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

### 4. 修改数值
```c++
  auto &vec = const_cast<std::vector<Search::SearchResult<int>>(currentResults.results());
vec[idx - 1].value = newVal;
  currentResults.modify([newVal](auto &r){ r.value = newVal; }).writeBack();
  currentResults.writeAll(12345);
  currentResults.writeOffset(0x24,12345);
```

详细例子 查看 main.cpp

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
