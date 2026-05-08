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

    // 移动
    Mem(Mem &&other) noexcept;
    Mem &operator=(Mem &&other) noexcept;

    // 原始字节读写，返回是否成功
    bool read(uintptr_t address, void *buffer, size_t size) override;
    bool write(uintptr_t address, const void *buffer, size_t size) override;

    bool write_assembly(uintptr_t address,const std::vector<std::string>& instructions);

    uintptr_t get_module_base(const char *module_name) const override;
    uintptr_t get_module_end(const char *module_name) const override;

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