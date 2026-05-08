#include "../core/Mem/Mem.hpp"
#include <iostream>

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
    return 0;
}