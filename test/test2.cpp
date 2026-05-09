#include "../core/Mem/Mem.hpp"
#include <iostream>
#include "../core/Mem/Search.hpp"

int main()
{
    auto processes = Process::list_processes();
    std::cout << "当前运行的进程列表:" << std::endl;
    for (const auto &proc : processes)
    {
        std::cout << "PID: " << proc.pid << " Name: " << proc.name << std::endl;
    }

    int pid;
    std::cout << "请输入目标进程 PID: ";
    std::cin >> pid;
    std::cin.ignore(); // 忽略换行符

    Mem mem(pid);
    Search search(mem);
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_ALL;
    params.parallel = true; // 启用多线程搜索

    // int value;
    // std::cout << "请输入要搜索的整数值: ";
    // std::cin >> value;
    // std::cin.ignore(); // 忽略换行符

    // std::time_t startTime = std::time(nullptr);
    // auto results = search.find<int>(params, value);
    // std::cout << "找到 " << results.size() << " 个结果:" << std::endl;

    // std::cout << "显示前 10 个结果:" << std::endl;
    // for (int i = 0; i < 10 && i < results.size(); ++i)
    // {
    //     const auto &res = results.results()[i];
    //     std::cout << "地址: 0x" << std::hex << res.address
    //               << " 值: " << std::dec << res.value << std::endl;
    // }
    // std::time_t endTime = std::time(nullptr);
    // std::cout << "搜索耗时: " << (endTime - startTime) << " 秒" << std::endl;

    // params.parallel = false; // 切换回单线程搜索
    // startTime = std::time(nullptr);
    // auto singleThreadResults = search.find<int>(params, value);
    // std::cout << "单线程搜索找到 " << singleThreadResults.size() << " 个结果:" << std::endl;

    //     std::cout << "显示前 10 个结果:" << std::endl;
    // for (int i = 0; i < 10 && i < singleThreadResults.size(); ++i)
    // {
    //     const auto &res = singleThreadResults.results()[i];
    //     std::cout << "地址: 0x" << std::hex << res.address
    //               << " 值: " << std::dec << res.value << std::endl;
    // }
    // endTime = std::time(nullptr);
    // std::cout << "单线程搜索耗时: " << (endTime - startTime) << " 秒" << std::endl;

    // 40E20100r;F00B40D1r;1F0240B9r;E00F1BF8r

    // std::time_t startTime2 = std::time(nullptr);
    // auto results2 = search.scanPattern(params, "40 E2 01 00 F0 0B 40 D1 1F 02 40 B9 E0 0F 1B F8");
    // std::cout << "找到 " << results2.size() << " 个结果:" << std::endl;
    // std::cout << "显示前 10 个结果:" << std::endl;
    // for (int i = 0; i < 10 && i < results2.size(); ++i)
    // {
    //     std::cout << "地址: 0x" << std::hex << results2[i] << std::endl;
    // }
    // std::time_t endTime2 = std::time(nullptr);
    // std::cout << "搜索耗时: " << (endTime2 - startTime2) << " 秒" << std::endl;

    // std::string pattern;
    // std::cout << "请输入要搜索的字符串: ";
    // std::cin >> pattern;
    // std::cout << "搜索字符串..." << std::endl;

    // std::time_t startTime3 = std::time(nullptr);
    // auto results = search.findStringUTF8(params, pattern, false, true);
    // for (const auto &addr : results)
    // {
    //     std::cout << "找到字符串地址: 0x" << std::hex << addr << std::endl;
    // }
    // std::cout << "搜索完成，共找到 " << std::dec << results.size() << " 个结果." << std::endl;
    // std::time_t endTime3 = std::time(nullptr);
    // std::cout << "搜索耗时: " << (endTime3 - startTime3) << " 秒" << std::endl;
    // std::string res;
    // std::cout << "输入修改后的字符串: ";
    // std::cin >> res;

    // int ok = 0;
    // for (const auto &addr : results)
    // {
    //    if( mem.write(addr, res.c_str(), res.size() + 1)) // 包括 null 结尾
    //         ++ok;
    //     else{
    //         std::cerr << "写入失败，地址: 0x" << std::hex << addr << std::dec << std::endl;
    //         std::cerr << "错误码: " << errno << " (" << strerror(errno) << ")" << std::endl;
    //         std::cerr << mem.get_address_map(addr).toString() << std::endl;
    //     }
    // }
    // std::cout << "成功写入 " << ok << " 个字符串." << std::endl;

    auto start = std::chrono::steady_clock::now();
    int num = 0;
    search.scanPatternAsync(params, "? ? 00 51", [&](uintptr_t addr)
                            {
                               num++;
                                return true; // 返回 false 可停止扫描
                            });
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "异步扫描耗时: " << ms / 1000.0 << " 秒" << std::endl;
    std::cout << "异步扫描找到 " << num << " 个结果." << std::endl;

    num = 0;
    auto start2 = std::chrono::steady_clock::now();
    auto results = search.scanPattern(params, "? ? 00 51");
    auto end2 = std::chrono::steady_clock::now();
    auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count();
    std::cout << "同步扫描耗时: " << ms2 / 1000.0 << " 秒" << std::endl;
    std::cout << "同步扫描找到 " << results.size() << " 个结果." << std::endl;
}