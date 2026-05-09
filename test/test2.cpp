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