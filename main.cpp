#include "core/Mem/Mem.hpp"
#include "core/Mem/Search.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <cctype>
#include <algorithm>
#include <thread>
#include <chrono>

// ==================== 辅助函数 ====================
std::vector<std::string> split(const std::string &s, char delim)
{
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim))
    {
        if (!token.empty())
            tokens.push_back(token);
    }
    return tokens;
}

std::string trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string toLower(const std::string &s)
{
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}

template <typename T>
bool parseNumber(const std::string &str, T &out)
{
    std::string s = trim(str);
    if (s.empty())
        return false;
    try
    {
        size_t pos;
        if (s.find("0x") == 0 || s.find("0X") == 0)
        {
            unsigned long long val = std::stoull(s.substr(2), &pos, 16);
            if (pos != s.size() - 2)
                return false;
            out = static_cast<T>(val);
        }
        else if (s.find('.') != std::string::npos && std::is_floating_point_v<T>)
        {
            double val = std::stod(s, &pos);
            if (pos != s.size())
                return false;
            out = static_cast<T>(val);
        }
        else
        {
            unsigned long long val = std::stoull(s, &pos, 10);
            if (pos != s.size())
                return false;
            out = static_cast<T>(val);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// ==================== 综合演示 ====================
void runDemo(Search &searcher, Mem &mem)
{
    std::cout << "\n========== Search 综合演示 ==========\n";

    // 1. 精确值搜索 (int)
    std::cout << "\n1. 搜索 int 值 123456 (全内存, 对齐):\n";
    Search::SearchParams params;
    params.memTypeMask = MemType::RANGE_ALL;
    params.align = true;
    auto intResults = searcher.find<int>(params, 123456);
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

    // 2. 自定义条件搜索 (float 在 [3.14, 3.16] 内)
    std::cout << "\n2. 自定义条件搜索 float 在 [3.14, 3.16] 内:\n";
    auto floatResults = searcher.find<float>(params, []( const Search::SearchResult<float> &res)
                                             { return res.value >= 3.14f && res.value <= 3.16f; });
    std::cout << "   找到 " << floatResults.size() << " 个结果\n";
    for (size_t i = 0; i < std::min(3UL, floatResults.size()); ++i)
    {
        std::cout << std::hex << "   0x" << floatResults.results()[i].address
                  << std::dec << " = " << floatResults.results()[i].value << "\n";
    }

    // 3. UTF-8 字符串搜索 (区分大小写, 包含空终止符)
    std::cout << "\n3. UTF-8 字符串搜索 \"Hello\" (区分大小写, 含 '\\0'):\n";
    auto utf8Addrs = searcher.findStringUTF8(params, "Hello", true, true);
    std::cout << "   找到 " << utf8Addrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(3UL, utf8Addrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << utf8Addrs[i] << std::dec << "\n";
    }

    // 4. UTF-16 字符串搜索
    std::cout << "\n4. UTF-16 字符串搜索 u\"World\" (小端序, 含空终止符):\n";
    auto utf16Addrs = searcher.findStringUTF16(params, u"World", true, true);
    std::cout << "   找到 " << utf16Addrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(3UL, utf16Addrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << utf16Addrs[i] << std::dec << "\n";
    }

    // 5. 特征码扫描 (精确模式 "90 90 90 90")
    std::cout << "\n5. 特征码扫描 \"90 90 90 90\" (NOP 滑动):\n";
    auto patternAddrs = searcher.scanPattern(params, "90 90 90 90");
    std::cout << "   找到 " << patternAddrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(5UL, patternAddrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << patternAddrs[i] << std::dec << "\n";
    }

    // 6. 带通配符的特征码扫描 "12 ?? 34"
    std::cout << "\n6. 带通配符 \"12 ?? 34\":\n";
    auto wildAddrs = searcher.scanPattern(params, "12 ?? 34");
    std::cout << "   找到 " << wildAddrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(5UL, wildAddrs.size()); ++i)
    {
        std::cout << std::hex << "   0x" << wildAddrs[i] << std::dec << "\n";
    }
    // 7. 异步扫描演示 (找到前 3 个就停止)
    std::cout << "\n7. 异步扫描 \"?? ?? ?? ??\" 最多返回 3 个:\n";
    int count = 0;
    searcher.scanPatternAsync(params, "?? ?? ?? ??", [&count](uintptr_t addr)
                              {
        std::cout << std::hex << "   找到 0x" << addr << std::dec << "\n";
        return (++count < 3); });
    std::cout << "   异步扫描完成，共找到 " << count << " 个地址\n";

    std::cout << "\n========== 演示结束 ==========\n";
}

// ==================== 主程序 ====================
int main(int argc, char *argv[])
{
    std::cout << "输入进程名或 PID ";
    std::string input;
    std::getline(std::cin, input);
    input = trim(input);

    int pid = 0;
    if (!parseNumber(input, pid) || pid <= 0)
    {
        pid = Process::get_pid_by_name(input.c_str());
        if (pid <= 0)
        {
            std::cerr << "未找到进程: " << input << std::endl;
            return 1;
        }
        std::cout << "找到 PID: " << pid << std::endl;
    }

    Mem mem(pid);
    Search searcher(mem);
    Search::ResultSet<int> currentResults(mem, {});

    auto printHelp = []()
    {
        std::cout << "\n可用命令:\n";
        std::cout << "  search <type> <value>      首次搜索 (type: int, float)\n";
        std::cout << "  refine <type> <value>      在结果中再次搜索 (仅 int)\n";
        std::cout << "  list [N]                   显示前 N 个结果\n";
        std::cout << "  modify <index> <newvalue>  修改指定索引的结果\n";
        std::cout << "  modifyall <newvalue>       修改所有结果\n";
        std::cout << "  write                      将修改写回进程\n";
        std::cout << "  refresh                    从进程重新读取值\n";
        std::cout << "  pattern <hex>              特征码扫描, 如 \"12 ?? 34\"\n";
        std::cout << "  utf8 <string>              搜索 UTF-8 字符串\n";
        std::cout << "  utf16 <string>             搜索 UTF-16 字符串\n";
        std::cout << "  dump <addr> [size]         dump 内存\n";
        std::cout << "  clear                      清空当前结果集\n";
        std::cout << "  demo                       运行综合演示\n";
        std::cout << "  help                       显示本帮助\n";
        std::cout << "  exit                       退出\n";
    };

    printHelp();

    std::string line;
    while (true)
    {
        std::cout << "\n> ";
        std::getline(std::cin, line);
        if (!std::cin)
            break;
        auto parts = split(line, ' ');
        if (parts.empty())
            continue;
        std::string cmd = toLower(parts[0]);

        if (cmd == "exit" || cmd == "quit")
        {
            break;
        }
        else if (cmd == "help")
        {
            printHelp();
        }
        else if (cmd == "demo")
        {
            runDemo(searcher, mem);
        }
        else if (cmd == "search")
        {
            if (parts.size() < 3)
            {
                std::cout << "用法: search <type> <value>\n";
                continue;
            }
            std::string type = toLower(parts[1]);
            std::string valStr = parts[2];
            Search::SearchParams params;
            params.memTypeMask = MemType::RANGE_ALL;
            params.align = true;

            if (type == "int")
            {
                int value;
                if (!parseNumber(valStr, value))
                {
                    std::cout << "无效整数\n";
                    continue;
                }
                currentResults = searcher.find<int>(params, value);
                std::cout << "找到 " << currentResults.size() << " 个结果\n";
            }
            else if (type == "float")
            {
                float value;
                if (!parseNumber(valStr, value))
                {
                    std::cout << "无效浮点数\n";
                    continue;
                }
                auto resFloat = searcher.find<float>(params, value);
                std::vector<Search::SearchResult<int>> converted;
                for (const auto &r : resFloat.results())
                    converted.push_back({r.address, static_cast<int>(r.value)});
                currentResults = Search::ResultSet<int>(mem, std::move(converted));
                std::cout << "找到 " << currentResults.size() << " 个结果 (转换为 int)\n";
            }
            else
            {
                std::cout << "不支持的类型\n";
            }
        }
        else if (cmd == "refine")
        {
            if (currentResults.empty())
            {
                std::cout << "无结果\n";
                continue;
            }
            if (parts.size() < 3)
            {
                std::cout << "用法: refine <type> <value>\n";
                continue;
            }
            std::string type = toLower(parts[1]);
            if (type != "int")
            {
                std::cout << "refine 仅支持 int\n";
                continue;
            }
            int value;
            if (!parseNumber(parts[2], value))
            {
                std::cout << "无效整数\n";
                continue;
            }
            currentResults.refresh();
            auto filtered = currentResults.filter([value](const auto &r)
                                                  { return r.value == value; });
            currentResults = std::move(filtered);
            std::cout << "过滤后剩余 " << currentResults.size() << " 个结果\n";
        }
        else if (cmd == "list")
        {
            currentResults.refresh();
            int limit = currentResults.size();
            if (parts.size() >= 2)
                parseNumber(parts[1], limit);
            const auto &vec = currentResults.results();
            size_t show = std::min((size_t)limit, vec.size());
            for (size_t i = 0; i < show; ++i)
            {
                std::cout << std::dec << i + 1 << ". 0x" << std::hex << vec[i].address
                          << " = " << std::dec << vec[i].value << "\n";
            }
        }
        else if (cmd == "modify")
        {
            if (currentResults.empty())
            {
                std::cout << "无结果\n";
                continue;
            }
            if (parts.size() < 3)
            {
                std::cout << "用法: modify <index> <newvalue>\n";
                continue;
            }
            int idx, newVal;
            if (!parseNumber(parts[1], idx) || idx < 1 || idx > (int)currentResults.size() ||
                !parseNumber(parts[2], newVal))
            {
                std::cout << "无效参数\n";
                continue;
            }
            auto &vec = const_cast<std::vector<Search::SearchResult<int>> &>(currentResults.results());
            vec[idx - 1].value = newVal;
            std::cout << "已修改（未写回）\n";
        }
        else if (cmd == "modifyall")
        {
            if (currentResults.empty())
            {
                std::cout << "无结果\n";
                continue;
            }
            if (parts.size() < 2)
            {
                std::cout << "用法: modifyall <newvalue>\n";
                continue;
            }
            int newVal;
            if (!parseNumber(parts[1], newVal))
            {
                std::cout << "无效新值\n";
                continue;
            }
            currentResults.modify([newVal](auto &r)
                                  { r.value = newVal; });
            std::cout << "已修改所有结果（未写回）\n";
        }
        else if (cmd == "write")
        {
            if (currentResults.empty())
            {
                std::cout << "无结果\n";
                continue;
            }
            if (currentResults.writeBack())
                std::cout << "写回成功\n";
            else
                std::cout << "写回失败\n";
        }
        else if (cmd == "refresh")
        {
            if (currentResults.empty())
            {
                std::cout << "无结果\n";
                continue;
            }
            currentResults.refresh();
            std::cout << "已刷新\n";
        }
        else if (cmd == "pattern")
        {
            if (parts.size() < 2)
            {
                std::cout << "用法: pattern <hexpattern>\n";
                continue;
            }
            Search::SearchParams patParams;
            patParams.memTypeMask = MemType::RANGE_ALL;
            patParams.align = false;
            auto addrs = searcher.scanPattern(patParams, parts[1]);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
            for (size_t i = 0; i < std::min(10UL, addrs.size()); ++i)
                std::cout << std::hex << "0x" << addrs[i] << std::dec << "\n";
        }
        else if (cmd == "utf8")
        {
            if (parts.size() < 2)
            {
                std::cout << "用法: utf8 <string>\n";
                continue;
            }
            Search::SearchParams strParams;
            strParams.memTypeMask = MemType::RANGE_ALL;
            strParams.align = false;
            auto addrs = searcher.findStringUTF8(strParams, parts[1], true, true);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
            for (size_t i = 0; i < std::min(10UL, addrs.size()); ++i)
                std::cout << std::hex << "0x" << addrs[i] << std::dec << "\n";
        }
        else if (cmd == "utf16")
        {
            if (parts.size() < 2)
            {
                std::cout << "用法: utf16 <string>\n";
                continue;
            }
            std::u16string u16str;
            for (char c : parts[1])
                u16str.push_back((char16_t)c);
            Search::SearchParams strParams;
            strParams.memTypeMask = MemType::RANGE_ALL;
            strParams.align = false;
            auto addrs = searcher.findStringUTF16(strParams, u16str, true, true);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
            for (size_t i = 0; i < std::min(10UL, addrs.size()); ++i)
                std::cout << std::hex << "0x" << addrs[i] << std::dec << "\n";
        }
        else if (cmd == "dump")
        {
            if (parts.size() < 2)
            {
                std::cout << "用法: dump <address> [size]\n";
                continue;
            }
            uintptr_t addr;
            size_t sz = 64;
            if (!parseNumber(parts[1], addr))
            {
                std::cout << "无效地址\n";
                continue;
            }
            if (parts.size() >= 3)
                parseNumber(parts[2], sz);
            std::vector<uint8_t> buf(sz);
            if (mem.read(addr, buf.data(), sz))
            {
                std::cout << "Dump 0x" << std::hex << addr << ":\n";
                for (size_t i = 0; i < sz; ++i)
                {
                    std::cout << std::setw(2) << std::setfill('0') << (int)buf[i] << " ";
                    if ((i + 1) % 16 == 0)
                        std::cout << "\n";
                }
                std::cout << std::dec << "\n";
            }
            else
            {
                std::cout << "读取失败\n";
            }
        }
        else if (cmd == "clear")
        {
            currentResults.clear();
            std::cout << "已清空\n";
        }
        else
        {
            std::cout << "未知命令，输入 help\n";
        }
    }
    return 0;
}