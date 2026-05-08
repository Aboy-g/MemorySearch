#pragma once
#ifndef PROCMAP_HPP
#define PROCMAP_HPP

#include <cstdint>
#include <string>
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
class ProcMap
{
public:
    uintptr_t startAddress;
    uintptr_t endAddress;
    size_t length;
    int protection;
    bool readable, writeable, executable, is_private, is_shared, is_ro, is_rw, is_rx;
    unsigned long long offset;
    std::string dev;
    unsigned long inode;
    std::string pathname;

    uint32_t getMemType() const
    {
        // 1. 特殊路径名或匿名区域
        if (pathname.empty())
        {
            // 可读写私有匿名 -> 匿名内存
            if (readable && writeable && !executable && is_private)
            {
                return MemType::RANGE_ANONYMOUS;
            }
            // 可执行私有匿名 -> JIT 代码（应用代码）
            if (executable && is_private)
            {
                return MemType::RANGE_CODE_APP;
            }
            return MemType::RANGE_OTHER;
        }

        // 2. 特定名称的映射（优先级最高）
        if (pathname == "[heap]")
        {
            return MemType::RANGE_C_HEAP;
        }
        if (pathname.find("[stack") == 0)
        {
            return MemType::RANGE_STACK;
        }
        if (pathname == "[anon:.bss]")
        {
            return MemType::RANGE_C_BSS;
        }
        if (pathname.find("[anon:libc_malloc]") == 0 ||
            pathname.find("[anon:scudo") == 0)
        {
            return MemType::RANGE_C_ALLOC;
        }
        if (pathname == "[vdso]" || pathname == "[vsyscall]" || pathname == "[vvar]")
        {
            return MemType::RANGE_CODE_SYSTEM;
        }

        // 3. Dalvik / Java 相关
        bool is_dalvik = (pathname.find("dalvik-") != std::string::npos);
        bool is_dex_jar_apk = (pathname.find(".dex") != std::string::npos ||
                               pathname.find(".jar") != std::string::npos ||
                               pathname.find(".apk") != std::string::npos);
        if (is_dalvik || is_dex_jar_apk)
        {
            if (readable && writeable && !executable)
            {
                return MemType::RANGE_JAVA_HEAP;
            }
            if (executable)
            {
                return MemType::RANGE_CODE_APP;
            }
            return MemType::RANGE_JAVA;
        }

        // 4. Ashmem 共享内存
        if (pathname.find("/dev/ashmem") != std::string::npos)
        {
            if (readable && writeable && !executable && !is_private)
            {
                return MemType::RANGE_ASHMEM;
            }
            // 可执行 ashmem（如快速代码加载）视为应用代码
            if (executable)
            {
                return MemType::RANGE_CODE_APP;
            }
            return MemType::RANGE_ASHMEM;
        }

        // 5. 视频内存
        if (pathname.find("/dev/mali") != std::string::npos ||
            pathname.find("/dev/ion") != std::string::npos)
        {
            return MemType::RANGE_VIDEO;
        }

        // 6. 坏内存区域（GPU 相关、字体文件等）
        if (pathname.find("kgsl-3d0") != std::string::npos ||
            pathname.find(".ttf") != std::string::npos)
        {
            return MemType::RANGE_B_BAD;
        }

        // 7. 普通文件映射：区分系统/应用
        bool is_system = (pathname.find("/system/") == 0 ||
                          pathname.find("/vendor/") == 0 ||
                          pathname.find("/apex/") == 0 ||
                          pathname.find("/product/") == 0 ||
                          pathname.find("/memfd") == 0); // 新增 memfd
        bool is_app = (pathname.find("/data/app/") == 0 ||
                       pathname.find("/data/data/") == 0 ||
                       pathname.find("/data/user/") == 0);

        // 代码段（可执行）
        if (executable)
        {
            if (is_app)
            {
                return MemType::RANGE_CODE_APP;
            }
            else if (is_system)
            {
                return MemType::RANGE_CODE_SYSTEM;
            }
            return MemType::RANGE_CODE_APP; // 默认应用代码
        }

        // 数据段（可读可写，不可执行，私有，有文件后援）
        if (readable && writeable && !executable && is_private && !pathname.empty())
        {
            return MemType::RANGE_C_DATA;
        }

        // 其他
        return MemType::RANGE_OTHER;
    }

    ProcMap() : startAddress(0), endAddress(0), length(0), protection(0),
                readable(false), writeable(false), executable(false),
                is_private(false), is_shared(false),
                is_ro(false), is_rw(false), is_rx(false),
                offset(0), inode(0) {}

    inline bool isValid() const { return (startAddress && endAddress && length); }
    inline bool isUnknown() const { return pathname.empty(); }
    inline bool contains(uintptr_t address) const { return address >= startAddress && address < endAddress; }
    inline std::string toString() const
    {
        char sharing = is_private ? 'p' : (is_shared ? 's' : '-');
        // 计算所需缓冲区大小（动态分配，避免截断）
        int needed = snprintf(nullptr, 0,
                              "%lx-%lx %c%c%c%c %llx %s %lu %s",
                              startAddress, endAddress,
                              readable ? 'r' : '-',
                              writeable ? 'w' : '-',
                              executable ? 'x' : '-',
                              sharing,
                              offset, dev.c_str(), inode, pathname.c_str());
        if (needed < 0)
            return std::string();
        std::string result(needed + 1, '\0');
        snprintf(&result[0], result.size(),
                 "%lx-%lx %c%c%c%c %llx %s %lu %s",
                 startAddress, endAddress,
                 readable ? 'r' : '-',
                 writeable ? 'w' : '-',
                 executable ? 'x' : '-',
                 sharing,
                 offset, dev.c_str(), inode, pathname.c_str());
        result.pop_back(); // 去除末尾多出的 '\0'
        return result;
    }
};

#endif