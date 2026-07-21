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
static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim))
        if (!token.empty()) tokens.push_back(token);
    return tokens;
}
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}
static std::string toLower(const std::string &s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), ::tolower);
    return res;
}
template <typename T>
static bool parseNumber(const std::string &str, T &out) {
    std::string s = trim(str);
    if (s.empty()) return false;
    try {
        size_t pos;
        if (s.find("0x") == 0 || s.find("0X") == 0) {
            unsigned long long val = std::stoull(s.substr(2), &pos, 16);
            if (pos != s.size() - 2) return false;
            out = static_cast<T>(val);
        } else if (s.find('.') != std::string::npos && std::is_floating_point_v<T>) {
            double val = std::stod(s, &pos);
            if (pos != s.size()) return false;
            out = static_cast<T>(val);
        } else {
            unsigned long long val = std::stoull(s, &pos, 10);
            if (pos != s.size()) return false;
            out = static_cast<T>(val);
        }
        return true;
    } catch (...) { return false; }
}

// ==================== 综合演示 ====================
static void runDemo(SearchEngine &search, Mem &mem) {
    (void)mem;
    std::cout << "\n========== Search 综合演示 ==========\n";

    SearchParams params;

    // 1. 精确值搜索
    std::cout << "\n1. 搜索 int 值 123456 (RW内存, 对齐):\n";
    auto intResults = search.search<int>(params, 123456);
    std::cout << "   找到 " << intResults.size() << " 个结果\n";
    if (!intResults.empty()) {
        std::cout << "   前5个: ";
        for (size_t i = 0; i < std::min(size_t(5), intResults.size()); ++i)
            std::cout << std::hex << "0x" << intResults[i].address << " ";
        std::cout << std::dec << "\n";
    }

    // 2. 模糊搜索
    std::cout << "\n2. 模糊搜索 float >= 3.14 (RW内存):\n";
    auto floatResults = search.searchCompare<float>(params, 3.14f, CompareOp::GTE);
    std::cout << "   找到 " << floatResults.size() << " 个结果\n";
    for (size_t i = 0; i < std::min(size_t(3), floatResults.size()); ++i)
        std::cout << std::hex << "   0x" << floatResults[i].address
                  << std::dec << " = " << floatResults[i].value << "\n";

    // 3. 字符串搜索
    std::cout << "\n3. UTF-8 字符串搜索 \"Hello\":\n";
    auto utf8Addrs = search.searchString(params, "Hello", true, true);
    std::cout << "   找到 " << utf8Addrs.size() << " 个地址\n";
    for (size_t i = 0; i < std::min(size_t(3), utf8Addrs.size()); ++i)
        std::cout << std::hex << "   0x" << utf8Addrs[i] << std::dec << "\n";

    // 4. UTF-16
    std::cout << "\n4. UTF-16 字符串搜索 u\"World\":\n";
    auto utf16Addrs = search.searchStringUTF16(params, u"World", true, true);
    std::cout << "   找到 " << utf16Addrs.size() << " 个地址\n";

    // 5. 特征码
    std::cout << "\n5. 特征码扫描 \"90 90 90 90\":\n";
    auto patternAddrs = search.scanPatternString(params, "90 90 90 90");
    std::cout << "   找到 " << patternAddrs.size() << " 个地址\n";

    // 6. 通配符
    std::cout << "\n6. 带通配符 \"12 ?? 34\":\n";
    auto wildAddrs = search.scanPatternString(params, "12 ?? 34");
    std::cout << "   找到 " << wildAddrs.size() << " 个地址\n";

    std::cout << "\n========== 演示结束 ==========\n";
}

// ==================== 主程序 ====================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    std::cout << "输入进程名或 PID: ";
    std::string input;
    std::getline(std::cin, input);
    input = trim(input);

    int pid = 0;
    if (!parseNumber(input, pid) || pid <= 0) {
        pid = Process::get_pid_by_name(input.c_str());
        if (pid <= 0) {
            std::cerr << "未找到进程: " << input << std::endl;
            return 1;
        }
        std::cout << "找到 PID: " << pid << std::endl;
    }

    Mem mem(pid);
    SearchEngine search(mem);
    ResultSet<int> currentResults(mem);

    auto printHelp = []() {
        std::cout << "\n可用命令:\n";
        std::cout << "  search <type> <value>      首次搜索 (int/float)\n";
        std::cout << "  fuzzy <type> <op> <val>    模糊搜索 (GT/LT/NEQ/GTE/LTE)\n";
        std::cout << "  range <type> <min> <max>   范围搜索\n";
        std::cout << "  refine <value>             在结果中过滤 (EQ)\n";
        std::cout << "  list [N]                   显示前 N 个结果\n";
        std::cout << "  modify <idx> <newval>      修改指定结果\n";
        std::cout << "  modifyall <newval>         修改所有结果\n";
        std::cout << "  write                      写回进程\n";
        std::cout << "  refresh                    重新读取值\n";
        std::cout << "  pattern <hex>              特征码扫描\n";
        std::cout << "  utf8 <string>              UTF-8 搜索\n";
        std::cout << "  utf16 <string>             UTF-16 搜索\n";
        std::cout << "  dump <addr> [size]         dump 内存\n";
        std::cout << "  stats                      显示性能统计\n";
        std::cout << "  clear                      清空结果集\n";
        std::cout << "  demo                       运行演示\n";
        std::cout << "  help                       帮助\n";
        std::cout << "  exit                       退出\n";
    };

    printHelp();

    std::string line;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, line);
        if (!std::cin) break;
        auto parts = split(line, ' ');
        if (parts.empty()) continue;
        std::string cmd = toLower(parts[0]);

        if (cmd == "exit" || cmd == "quit") {
            break;
        } else if (cmd == "help") {
            printHelp();
        } else if (cmd == "demo") {
            runDemo(search, mem);
        } else if (cmd == "search") {
            if (parts.size() < 3) { std::cout << "用法: search <type> <value>\n"; continue; }
            std::string type = toLower(parts[1]);
            SearchParams p;
            if (type == "int") {
                int value; if (!parseNumber(parts[2], value)) { std::cout << "无效整数\n"; continue; }
                currentResults = search.search<int>(p, value);
            } else if (type == "float") {
                float value; if (!parseNumber(parts[2], value)) { std::cout << "无效浮点\n"; continue; }
                auto rf = search.search<float>(p, value);
                std::vector<SearchResult<int>> cv;
                for (const auto& r : rf.results()) cv.push_back({r.address, (int)r.value});
                currentResults = ResultSet<int>(mem, std::move(cv));
            } else { std::cout << "不支持的类型\n"; continue; }
            std::cout << "找到 " << currentResults.size() << " 个结果\n";
        } else if (cmd == "fuzzy") {
            if (parts.size() < 4) { std::cout << "用法: fuzzy <type> <op> <value>\n"; continue; }
            std::string opStr = toLower(parts[2]);
            CompareOp op;
            if (opStr == "gt") op = CompareOp::GT;
            else if (opStr == "lt") op = CompareOp::LT;
            else if (opStr == "neq") op = CompareOp::NEQ;
            else if (opStr == "gte") op = CompareOp::GTE;
            else if (opStr == "lte") op = CompareOp::LTE;
            else { std::cout << "不支持的操作符 (gt/lt/neq/gte/lte)\n"; continue; }
            SearchParams p;
            if (toLower(parts[1]) == "int") {
                int value; if (!parseNumber(parts[3], value)) { std::cout << "无效值\n"; continue; }
                currentResults = search.searchCompare<int>(p, value, op);
            } else if (toLower(parts[1]) == "float") {
                float value; if (!parseNumber(parts[3], value)) { std::cout << "无效值\n"; continue; }
                auto rf = search.searchCompare<float>(p, value, op);
                std::vector<SearchResult<int>> cv;
                for (const auto& r : rf.results()) cv.push_back({r.address, (int)r.value});
                currentResults = ResultSet<int>(mem, std::move(cv));
            }
            std::cout << "找到 " << currentResults.size() << " 个结果\n";
        } else if (cmd == "refine") {
            if (currentResults.empty()) { std::cout << "无结果\n"; continue; }
            if (parts.size() < 2) { std::cout << "用法: refine <value>\n"; continue; }
            int value; if (!parseNumber(parts[1], value)) { std::cout << "无效值\n"; continue; }
            currentResults.refresh();
            auto filtered = currentResults.filter([value](const auto& r){ return r.value == value; });
            currentResults = std::move(filtered);
            std::cout << "过滤后剩余 " << currentResults.size() << " 个结果\n";
        } else if (cmd == "list") {
            currentResults.refresh();
            int limit = (int)currentResults.size();
            if (parts.size() >= 2) parseNumber(parts[1], limit);
            for (size_t i = 0; i < std::min((size_t)limit, currentResults.size()); ++i)
                std::cout << std::dec << (i+1) << ". 0x" << std::hex << currentResults[i].address
                          << " = " << std::dec << currentResults[i].value << "\n";
        } else if (cmd == "modify") {
            if (currentResults.empty()) { std::cout << "无结果\n"; continue; }
            if (parts.size() < 3) { std::cout << "用法: modify <index> <newvalue>\n"; continue; }
            int idx, newVal;
            if (!parseNumber(parts[1], idx) || idx < 1 || idx > (int)currentResults.size() ||
                !parseNumber(parts[2], newVal)) { std::cout << "无效参数\n"; continue; }
            currentResults[idx - 1].value = newVal;
            std::cout << "已修改 (未写回)\n";
        } else if (cmd == "modifyall") {
            if (currentResults.empty()) { std::cout << "无结果\n"; continue; }
            if (parts.size() < 2) { std::cout << "用法: modifyall <newvalue>\n"; continue; }
            int newVal; if (!parseNumber(parts[1], newVal)) { std::cout << "无效值\n"; continue; }
            currentResults.modify([newVal](auto& r){ r.value = newVal; });
            std::cout << "已修改 " << currentResults.size() << " 个结果\n";
        } else if (cmd == "write") {
            if (currentResults.empty()) { std::cout << "无结果\n"; continue; }
            std::cout << (currentResults.writeBack() ? "写回成功\n" : "写回失败\n");
        } else if (cmd == "refresh") {
            if (currentResults.empty()) { std::cout << "无结果\n"; continue; }
            currentResults.refresh();
            std::cout << "已刷新\n";
        } else if (cmd == "pattern") {
            if (parts.size() < 2) { std::cout << "用法: pattern <hex>\n"; continue; }
            SearchParams p; p.memTypeMask = MemType::RANGE_ALL;
            auto addrs = search.scanPatternString(p, parts[1]);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
            for (size_t i = 0; i < std::min(size_t(10), addrs.size()); ++i)
                std::cout << std::hex << "0x" << addrs[i] << std::dec << "\n";
        } else if (cmd == "utf8") {
            if (parts.size() < 2) { std::cout << "用法: utf8 <string>\n"; continue; }
            SearchParams p; p.memTypeMask = MemType::RANGE_ALL;
            auto addrs = search.searchString(p, parts[1], true, true);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
            for (size_t i = 0; i < std::min(size_t(10), addrs.size()); ++i)
                std::cout << std::hex << "0x" << addrs[i] << std::dec << "\n";
        } else if (cmd == "utf16") {
            if (parts.size() < 2) { std::cout << "用法: utf16 <string>\n"; continue; }
            std::u16string u16str;
            for (char c : parts[1]) u16str.push_back((char16_t)c);
            SearchParams p; p.memTypeMask = MemType::RANGE_ALL;
            auto addrs = search.searchStringUTF16(p, u16str, true, true);
            std::cout << "找到 " << addrs.size() << " 个匹配\n";
        } else if (cmd == "dump") {
            if (parts.size() < 2) { std::cout << "用法: dump <addr> [size]\n"; continue; }
            uintptr_t addr; size_t sz = 64;
            if (!parseNumber(parts[1], addr)) { std::cout << "无效地址\n"; continue; }
            if (parts.size() >= 3) parseNumber(parts[2], sz);
            std::vector<uint8_t> buf(sz);
            if (mem.read(addr, buf.data(), sz)) {
                std::cout << "Dump 0x" << std::hex << addr << ":\n";
                for (size_t i = 0; i < sz; ++i) {
                    std::cout << std::setw(2) << std::setfill('0') << (int)buf[i] << " ";
                    if ((i+1) % 16 == 0) std::cout << "\n";
                }
                std::cout << std::dec << "\n";
            } else { std::cout << "读取失败\n"; }
        } else if (cmd == "stats") {
            search.lastStats().print();
        } else if (cmd == "clear") {
            currentResults.clear();
            std::cout << "已清空\n";
        } else {
            std::cout << "未知命令，输入 help\n";
        }
    }
    return 0;
}
