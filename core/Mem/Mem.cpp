#include "Mem.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <cstdio>
#include <cerrno>
#include "..\Keystone\includes\keystone.h"
Mem::Mem()
{
    set_pid(getpid());
    open_mem();
}
// 构造函数
Mem::Mem(int pid)
{
    set_pid(pid);
    open_mem();
}

// 析构函数
Mem::~Mem()
{
    close_mem();
}

// 打开 /proc/pid/mem 文件描述符
void Mem::open_mem()
{
    if (mem_fd >= 0)
        return; // 已打开
    if (pid < 0)
        return;

    std::string path = "/proc/" + std::to_string(pid) + "/mem";
    mem_fd = open(path.c_str(), O_RDWR);
    if (mem_fd < 0)
    {
        perror("Failed to open /proc/pid/mem");
    }
}

// 关闭文件描述符
void Mem::close_mem()
{
    if (mem_fd >= 0)
    {
        close(mem_fd);
        mem_fd = -1;
    }
}

// 使用 process_vm_readv / writev 系统调用
bool Mem::pvm(void *address, void *buffer, size_t size, bool iswrite)
{
    if (pid < 0)
        return false;

    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = address;
    remote[0].iov_len = size;

#if defined(__arm__)
    long syscall_readv = 376;
    long syscall_writev = 377;
#elif defined(__aarch64__)
    long syscall_readv = 270;
    long syscall_writev = 271;
#elif defined(__i386__)
    long syscall_readv = 347;
    long syscall_writev = 348;
#elif defined(__x86_64__)
    long syscall_readv = 310;
    long syscall_writev = 311;
#else
#error "Unsupported architecture"
#endif

    long syscall_no = iswrite ? syscall_writev : syscall_readv;
    ssize_t ret = syscall(syscall_no, pid, local, 1, remote, 1, 0);
    if (ret == static_cast<ssize_t>(size))
    {
        return true;
    }
    return false;
}

// 使用系统调用读取
bool Mem::read_sys(uintptr_t address, void *buffer, size_t size)
{
    return pvm(reinterpret_cast<void *>(address), buffer, size, false);
}

// 使用系统调用写入
bool Mem::write_sys(uintptr_t address, const void *buffer, size_t size)
{
    return pvm(reinterpret_cast<void *>(address), const_cast<void *>(buffer), size, true);
}

uintptr_t Mem::get_module_base(const char *module_name) const
{
    FILE *fp;
    uintptr_t addr = 0;
    char filename[32], buffer[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "rt");
    if (fp != nullptr)
    {
        while (fgets(buffer, sizeof(buffer), fp))
        {
            if (strstr(buffer, module_name))
            {
#if defined(__LP64__)
                sscanf(buffer, "%lx-%*s", &addr);
#else
                sscanf(buffer, "%x-%*s", &addr);
#endif
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

uintptr_t Mem::get_module_end(const char *module_name) const
{
    FILE *fp;
    uintptr_t temp = 0, addr = 0;
    char filename[32], buffer[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    fp = fopen(filename, "rt");
    if (fp != nullptr)
    {
        while (fgets(buffer, sizeof(buffer), fp))
        {
            if (strstr(buffer, module_name))
            {
#if defined(__LP64__)
                sscanf(buffer, "%lx-%lx %*s", &temp, &addr);
#else
                sscanf(buffer, "%x-%x %*s", &temp, &addr);
#endif
            }
        }
        fclose(fp);
    }
    return addr;
}

// 使用 /proc/pid/mem 读取
bool Mem::read_mem(uintptr_t address, void *buffer, size_t size)
{
    if (mem_fd < 0)
        return false;
    ssize_t n = pread64(mem_fd, buffer, size, static_cast<off64_t>(address));
    return (n == static_cast<ssize_t>(size));
}

// 使用 /proc/pid/mem 写入
bool Mem::write_mem(uintptr_t address, const void *buffer, size_t size)
{
    if (mem_fd < 0)
        return false;
    ssize_t n = pwrite64(mem_fd, buffer, size, static_cast<off64_t>(address));
    return (n == static_cast<ssize_t>(size));
}

// 公开读取接口：优先尝试系统调用，失败则回退到文件
bool Mem::read(uintptr_t address, void *buffer, size_t size)
{
    if (read_sys(address, buffer, size))
    {
        return true;
    }
    return read_mem(address, buffer, size);
}

// 公开写入接口：优先尝试系统调用，失败则回退到文件
bool Mem::write(uintptr_t address, const void *buffer, size_t size)
{
    if (write_sys(address, buffer, size))
    {
        return true;
    }
    return write_mem(address, buffer, size);
}

bool Mem::write_assembly(uintptr_t address, const std::vector<std::string> &instructions)
{
    if (instructions.empty())
        return false;
    if (pid < 0)
        return false;

    // 合并汇编代码
    std::string asm_code;
    for (const auto &line : instructions)
    {
        if (!asm_code.empty())
            asm_code += "; ";
        asm_code += line;
    }

    ks_engine *ks = nullptr;
    ks_arch arch;
    ks_mode mode;

#if defined(__aarch64__)
    arch = KS_ARCH_ARM64;
    mode = KS_MODE_LITTLE_ENDIAN;
#elif defined(__arm__)
    arch = KS_ARCH_ARM;
    mode = KS_MODE_ARM; // 如果是 Thumb 模式则用 KS_MODE_THUMB
#elif defined(__i386__)
    arch = KS_ARCH_X86;
    mode = KS_MODE_32;
#elif defined(__x86_64__)
    arch = KS_ARCH_X86;
    mode = KS_MODE_64;
#else
#error "Unsupported architecture"
#endif

    if (ks_open(arch, mode, &ks) != KS_ERR_OK)
    {
        fprintf(stderr, "Keystone: ks_open failed\n");
        return false;
    }

    unsigned char *encode = nullptr;
    size_t size = 0, stat_count = 0;
    if (ks_asm(ks, asm_code.c_str(), address, &encode, &size, &stat_count) != KS_ERR_OK)
    {
        ks_err err = ks_errno(ks);
        fprintf(stderr, "Keystone: ks_asm failed: %s\n", ks_strerror(err));
        ks_close(ks);
        return false;
    }

    bool write_ok = write(address, encode, size);
    ks_free(encode);
    ks_close(ks);
    return write_ok;
}