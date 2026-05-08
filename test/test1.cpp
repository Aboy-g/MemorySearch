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
    // std::cout << "请输入要写入的地址 (16 进制): ";
    // std::cin >> std::hex >> address;
    // Mem mem(pid);

    // std::vector<std::string> instructions = {
    //    "ADD	 W9, W9, #0x1"
    // };
    // mem.write_assembly(address, instructions);

    Mem mem(pid);
    Search search(mem);
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_C_ALLOC;
    // auto results = search.find<int>(params, -975558080);
    // std::cout << "找到 " << results.size() << " 个结果:" << std::endl;
    // std::cout << "改善" << std::endl;
    // results.filterSelf([&mem](const auto &res)
    // { 
    //     return mem.Read<int>(res.address + 4) == -1275068294;
    // }).filterSelf([&mem](const auto &res)
    // {
    //     return mem.Read<int>(res.address + 12) == 0;
    // });

    // std::cout << "过滤后剩余 " << results.size() << " 个结果:" << std::endl;
    // for (const auto &res : results.results())
    // {
    //     std::cout << "地址: 0x" << std::hex << res.address
    //               << " 值: " << std::dec << res.value << std::endl;
    // }

    // results.writeOffset(0x8,9999);
    // std::string pattern;
    // std::cout << "请输入要搜索的字符串: ";
    // std::cin >> pattern;
    // std::cout << "搜索字符串..." << std::endl;
    // auto results = search.findStringUTF8(params, pattern, false, true);
    // for (const auto &addr : results)
    // {
    //     std::cout << "找到字符串地址: 0x" << std::hex << addr << std::endl;
    // }
    // std::cout << "搜索完成，共找到 " << std::dec << results.size() << " 个结果." << std::endl;
    // std::string res;
    // std::cout << "输入修改后的字符串: ";
    // std::cin >> res;
    // for (const auto &addr : results)
    // {
    //     mem.write(addr, res.c_str(), res.size() + 1); // 包括 null 结尾
    // }


    // 29050051r;090100B9r;C0035FD6r
    auto results = search.scanPattern({.align = false, .memTypeMask = MemType::RANGE_CODE_APP}, "29 05 00 51 ?? ?? ?? ?? C0 03 5F D6");
    for (const auto &addr : results)
    {
        std::cout << "找到模式地址: 0x" << std::hex << addr << std::endl;
    }
    bool is_w = false;
    std::cout << "是否写入新值？(y/n): ";
    char choice;
    std::cin >> choice;
    if (choice == 'y' || choice == 'Y')    {
        is_w = true;
    }
    std::string newValue;
    if (is_w) {
        std::cout << "输入汇编指令（以 ';' 分隔）: ";
        std::cin.ignore(); // 忽略之前输入的换行符
        std::getline(std::cin, newValue);
        std::vector<std::string> instructions;
        size_t pos = 0; 
        while ((pos = newValue.find(';')) != std::string::npos) {
            std::string instr = newValue.substr(0, pos);
            instructions.push_back(instr);
            newValue.erase(0, pos + 1);
        }
        instructions.push_back(newValue); // 添加最后一个指令

        for (const auto &instr : instructions) {
            std::cout << "指令: " << instr << std::endl;
        }
        for (const auto &addr : results)
        {
            mem.write_assembly(addr, instructions);
            std::cout << "已写入新指令到地址: 0x" << std::hex << addr << std::endl;
        }
    }
    return 0;
}