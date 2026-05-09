#ifndef MEM_H
#define MEM_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include "ProcMap.hpp"
#include "Membase.hpp"
#include "Process.hpp"
class Mem : public MemBase
{
public:
    Mem();
    explicit Mem(int pid); // explicit 防止隐式转换
    Mem(std::string process_name) : Mem(Process::get_pid_by_name(process_name.c_str())) {}
    ~Mem();

    // 禁止拷贝
    Mem(const Mem &) = delete;
    Mem &operator=(const Mem &) = delete;

    Mem(Mem &&other) noexcept
        : MemBase(other), mem_fd(other.mem_fd)
    {
        other.set_pid(-1);
        other.mem_fd = -1;
    }

    Mem &operator=(Mem &&other) noexcept
    {
        if (this != &other)
        {
            close_mem();
            set_pid(other.get_pid());
            mem_fd = other.mem_fd;
            other.set_pid(-1);
            other.mem_fd = -1;
        }
        return *this;
    }

    // 原始字节读写，返回是否成功
    bool read(uintptr_t address, void *buffer, size_t size) override;
    bool write(uintptr_t address, const void *buffer, size_t size) override;

    bool write_assembly(uintptr_t address, const std::vector<std::string> &instructions);

private:
    int mem_fd = -1;

    bool pvm(void *address, void *buffer, size_t size, bool iswrite);
    void open_mem();
    void close_mem();

    bool read_mem(uintptr_t address, void *buffer, size_t size);
    bool write_mem(uintptr_t address, const void *buffer, size_t size);
    bool read_sys(uintptr_t address, void *buffer, size_t size);
    bool write_sys(uintptr_t address, const void *buffer, size_t size);
};

#endif