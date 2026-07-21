#include "../core/Mem/Mem.hpp"
#include <iostream>
#include "../core/Mem/Search.hpp"

int main()
{
    // auto processes = Process::list_processes();
    // std::cout << "当前运行的进程列表:" << std::endl;
    // for (const auto &proc : processes)
    // {
    //     std::cout << "PID: " << proc.pid << " Name: " << proc.name << std::endl;
    // }

    int pid;
    std::cout << "请输入目标进程 PID: ";
    std::cin >> pid;
    std::cin.ignore(); // 忽略换行符

    Mem mem(pid);
    SearchEngine search(mem);
    SearchParams params;
    params.memTypeMask = MemType::RANGE_ALL;
    params.parallel = true; // 启用多线程搜索

    // std::string module_name;
    // std::cout << "请输入要搜索的模块名称（留空搜索整个进程）: ";
    // std::getline(std::cin, module_name);

    //     if (!module_name.empty())
    //     {
    //         uintptr_t base = mem.get_module_base(module_name.c_str());
    //         uintptr_t end = mem.get_module_end(module_name.c_str());
    //         if (base == 0 || end == 0 || base >= end)
    //         {
    //             std::cout << "无法获取模块 " << module_name << " 的地址范围，搜索整个进程\n";
    //         }
    //         else
    //         {
    //             params.startAddress = base;
    //             params.endAddress = end;
    //             std::cout << "搜索模块 " << module_name << " 范围: 0x" << std::hex << base << " - 0x" << end << std::dec << "\n";
    //         }

    //         for (const auto &map : mem.get_module_maps(module_name.c_str()))
    //         {
    //             std::cout << map.toString() << std::endl;
    //         }
    //     }

    // std::string pattern;
    // std::cout << "请输入要搜索的字符串: ";
    // std::getline(std::cin, pattern);
    // std::cout << "搜索字符串..." << std::endl;
    // auto results = search.findStringUTF8(params, pattern, false, true);
    // for (const auto &addr : results)
    // {
    //     std::cout << "找到字符串地址: 0x" << std::hex << addr << std::endl;
    // }
    // std::cout << "搜索完成，共找到 " << std::dec << results.size() << " 个结果." << std::endl;
    // int value;
    // std::cout << "请输入要搜索的整数值: ";
    // std::cin >> value;
    // std::cout << "搜索整数值..." << std::endl;
    // std::time_t startTime = std::time(nullptr);
    // auto results = search.find<int>(params, value);
    // std::time_t endTime = std::time(nullptr);

    // for (int i = 0; i < 20 && i < results.size(); ++i)
    // {
    //     const auto &res = results[i];
    //     std::cout << "地址: 0x" << std::hex << res.address
    //               << " 值: " << std::dec << res.value << std::endl;
    // }
    //  std::cout << "搜索完成，共找到 " << results.size() << " 个结果. 耗时 " << (endTime - startTime) << " 秒." << std::endl;
    std::time_t startTime = std::time(nullptr);
    // 0820201Er;0141881Ar;01900279r;C0035FD6r

    params.memTypeMask = MemType::RANGE_CODE_APP;
    search.searchPatternAsync(params, "08 20 20 1E 01 41 88 1A 01 90 02 79 C0 03 5F D6", [&mem](uintptr_t addr)
                                              {
        std::cout << "找到地址: 0x" << std::hex << addr << std::dec << "\n";
        mem.write_assembly(addr, {
            "NOP",
            "ADD W1, W1, W1",
        });
        return true; // 返回 false 可提前停止扫描
    });
}