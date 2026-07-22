/**
 * GameGuardian-style 简易控制台修改器
 *
 * 工作流: 选进程 → 选类型 → 选区域 → 搜索 → 精炼 → 修改
 *
 * 用法: gg_tool
 */

#include "../core/Mem/MemMmap.hpp"
#include "../core/Mem/Search.hpp"
#include "../core/Mem/FuzzySearch.hpp"
#include <iostream>
#include <sstream>
#include <atomic>
#include <thread>

#define RST  "\033[0m"
#define BLD  "\033[1m"
#define RED  "\033[31m"
#define GRN  "\033[32m"
#define YEL  "\033[33m"
#define CYN  "\033[36m"
#define GRY  "\033[90m"

// ==================== 工具 ====================
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
template<typename T> static bool parse(const std::string& s, T& out) {
    std::string t = trim(s); if (t.empty()) return false;
    try { size_t p; double d = std::stod(t, &p); if (p != t.size()) return false; out = static_cast<T>(d); return true; }
    catch (...) { return false; }
}
static std::string fmtN(size_t n) {
    if (n < 10000) return std::to_string(n);
    if (n < 1000000) { char b[32]; snprintf(b, 32, "%.1fK", n / 1000.0); return b; }
    char b[32]; snprintf(b, 32, "%.2fM", n / 1000000.0); return b;
}
static std::string fmtS(size_t b) {
    const char* u[] = {"B", "KB", "MB", "GB"};
    int i = 0; double s = b;
    while (s >= 1024 && i < 3) { s /= 1024; i++; }
    char buf[64]; snprintf(buf, sizeof(buf), "%.1f%s", s, u[i]); return buf;
}
static bool bar(double p) {
    int w = 30, f = (int)(p * w);
    printf("\r  ["); for (int i = 0; i < w; i++) printf("%s", i < f ? "=" : " ");
    printf("] %3.0f%%", p * 100); fflush(stdout); return true;
}

// ==================== 类型定义 ====================
enum class VType { DWORD, FLOAT, QWORD, DOUBLE };
static const char* tname(VType t) {
    switch (t) { case VType::DWORD: return "DWORD"; case VType::FLOAT: return "FLOAT";
                 case VType::QWORD: return "QWORD"; case VType::DOUBLE: return "DOUBLE"; }
    return "?";
}

// ==================== 区域 ====================
static const char* rname(uint32_t m) {
    if (m == MemType::RANGE_ALL)       return "ALL";
    if (m == MemType::RANGE_RW)        return "A:Anonymous/RW";
    if (m == MemType::RANGE_JAVA_HEAP) return "Jh:JavaHeap";
    if (m == MemType::RANGE_C_ALLOC)   return "Ca:CAlloc";
    if (m == MemType::RANGE_C_DATA)    return "Cd:CData";
    if (m == MemType::RANGE_C_BSS)     return "Cb:CBss";
    if (m == MemType::RANGE_ANONYMOUS) return "A:Anon";
    if (m == MemType::RANGE_CODE_APP)  return "Xa:Code";
    if (m == MemType::RANGE_STACK)     return "S:Stack";
    return "?";
}

// ==================== 主逻辑 ====================
class GGTool {
public:
    GGTool(MemBase& m) : fuzzy(m), search(m) {
        params.chunkSize = 32 * 1024 * 1024;
        params.memTypeMask = MemType::RANGE_ALL;
    }

    void run() {
        printf(BLD CYN "\n  ▸ GameGuardian-style 修改器\n" RST);
        printf(GRY "  s=搜索  r=精炼  l=列出  m=修改  w=写回  q=退出  ?=帮助\n\n" RST);

        while (true) {
            showLine();
            printf(CYN "  > " RST);
            std::string line;
            std::getline(std::cin, line);
            if (!std::cin) break;
            auto parts = split(line);
            if (parts.empty()) continue;
            std::string cmd = parts[0];

            if (cmd == "q") break;
            else if (cmd == "?") help();
            else if (cmd == "t") setType(parts);
            else if (cmd == "r") setRegion(parts);
            else if (cmd == "s") searchValue(parts);
            else if (cmd == "u") searchUnknown(parts);
            else if (cmd == "f" || cmd == "r") refine(parts);
            else if (cmd == "l") showResults(parts);
            else if (cmd == "m") modify(parts);
            else if (cmd == "w") writeBack();
            else if (cmd == "c") { fuzzy.reset(); printf(GRN "  已清空\n" RST); }
            else printf(RED "  ? 未知命令, 输入 ? 查看帮助\n" RST);
        }
    }

private:
    FuzzySearch fuzzy;
    SearchEngine search;
    SearchParams params;
    VType vtype = VType::DWORD;

    std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> r; std::stringstream ss(s); std::string t;
        while (ss >> t) r.push_back(t); return r;
    }

    void showLine() {
        size_t n = fuzzy.size();
        printf(GRY "  [%s|%s] " RST "%s" RST "\n",
               tname(vtype), rname(params.memTypeMask),
               n > 0 ? (BLD + fmtN(n)).c_str() : "0");
    }

    void help() {
        printf(BLD "\n  ═══ GameGuardian 修改器 ═══\n\n" RST);
        printf("  " CYN "t d|f|q|dd" RST "     类型: DWORD/FLOAT/QWORD/DOUBLE\n");
        printf("  " CYN "r A|Jh|Ca|Cd|Xa" RST " 区域 (r 查看全部)\n");
        printf("  " CYN "s <value>" RST "      精确值搜索\n");
        printf("  " CYN "s >N / s <N" RST "    大于/小于搜索\n");
        printf("  " CYN "u" RST "              未知值搜索 (自动快照)\n");
        printf("  " CYN "f + | - | ~ | =" RST " 精炼: 变大/变小/变化/未变\n");
        printf("  " CYN "f <value>" RST "      精炼为精确值\n");
        printf("  " CYN "f >N / f <N" RST "    精炼为大于/小于\n");
        printf("  " CYN "l [N]" RST "          列出结果\n");
        printf("  " CYN "m <i> <v>" RST "      修改第i个\n");
        printf("  " CYN "ma <v>" RST "         修改全部\n");
        printf("  " CYN "w" RST "              写回\n");
        printf("  " CYN "c" RST "              清空\n");
        printf("  " CYN "q" RST "              退出\n\n");
        printf(GRY "  经典流程: s 100 → (改值) → f + → l → m 1 999 → w\n" RST "\n");
    }

    void setType(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  d|f|q|dd\n"); return; }
        auto t = parts[1];
        fuzzy.reset();
        if (t == "d") vtype = VType::DWORD;
        else if (t == "f") vtype = VType::FLOAT;
        else if (t == "q") vtype = VType::QWORD;
        else if (t == "dd") vtype = VType::DOUBLE;
        else { printf(RED "  d|f|q|dd\n" RST); return; }
        printf(GRN "  ✓ %s\n" RST, tname(vtype));
    }

    void setRegion(const std::vector<std::string>& parts) {
        if (parts.size() < 2) {
            printf("  ALL A Jh Ca Cd Cb Anon S Xa\n"); return;
        }
        auto t = parts[1];
        fuzzy.reset();
        if (t == "ALL") params.memTypeMask = MemType::RANGE_ALL;
        else if (t == "A") params.memTypeMask = MemType::RANGE_RW;
        else if (t == "Jh") params.memTypeMask = MemType::RANGE_JAVA_HEAP;
        else if (t == "Ca") params.memTypeMask = MemType::RANGE_C_ALLOC;
        else if (t == "Cd") params.memTypeMask = MemType::RANGE_C_DATA;
        else if (t == "Cb") params.memTypeMask = MemType::RANGE_C_BSS;
        else if (t == "Anon") params.memTypeMask = MemType::RANGE_ANONYMOUS;
        else if (t == "S") params.memTypeMask = MemType::RANGE_STACK;
        else if (t == "Xa") params.memTypeMask = MemType::RANGE_CODE_APP;
        else { printf(RED "  无效区域\n" RST); return; }
        printf(GRN "  ✓ %s\n" RST, rname(params.memTypeMask));
    }

    // ── 搜索 ──
    void searchValue(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  s <value> | s >N | s <N\n"); return; }
        fuzzy.reset();

        auto op = parts[1];
        // 大于/小于搜索
        if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
            bool gt = op[0] == '>';
            std::string vs = op.substr(1);
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(vs, v)) { printf(RED "  无效值\n" RST); return; }
                auto r = search.searchCompare<T>(params, v, gt ? CompareOp::GT : CompareOp::LT, bar);
                printf("\r" GRN "  ✓ %s 结果\n" RST, fmtN(r.size()).c_str());
                storeResults(r);
            });
            return;
        }

        // 精确值搜索
        callTyped([&](auto dummy) {
            using T = decltype(dummy);
            T v; if (!parse(op, v)) { printf(RED "  无效值\n" RST); return; }
            fuzzy.searchValue<T>(params, v, bar);
            printf("\r" GRN "  ✓ %s 条 | 快照已创建\n" RST, fmtN(fuzzy.size()).c_str());
        });
    }

    void searchUnknown(const std::vector<std::string>&) {
        fuzzy.reset();
        callTyped([&](auto dummy) {
            using T = decltype(dummy);
            fuzzy.searchUnknown<T>(params, 0, bar);
        });
        printf("\r" GRN "  ✓ %s 条 | 快照已创建\n" RST, fmtN(fuzzy.size()).c_str());
    }

    // ── 精炼 ──
    void refine(const std::vector<std::string>& parts) {
        if (fuzzy.size() == 0 && fuzzy.phase() != FuzzySearch::Phase::REGION_SNAPSHOT) {
            printf(RED "  先搜索 (s/u)\n" RST); return;
        }
        if (parts.size() < 2) { printf("  f +|-|~|=|<value>|>N|<N\n"); return; }

        auto op = parts[1];
        size_t before = fuzzy.size();
        if (fuzzy.phase() == FuzzySearch::Phase::REGION_SNAPSHOT && before == 0)
            before = fuzzy.stats().phase1Results;

        if (op == "+" || op == "-" || op == "~" || op == "=") {
            CompareOp cop = op == "+" ? CompareOp::INCREASED : op == "-" ? CompareOp::DECREASED
                          : op == "~" ? CompareOp::CHANGED : CompareOp::UNCHANGED;
            callTyped([&](auto dummy) { fuzzy.refine<decltype(dummy)>(cop); });
        } else if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
            bool gt = op[0] == '>';
            std::string vs = op.substr(1);
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(vs, v)) { printf(RED "  无效\n" RST); return; }
                fuzzy.filterCompare<T>(gt ? CompareOp::GT : CompareOp::LT, v);
            });
        } else {
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(op, v)) { printf(RED "  无效\n" RST); return; }
                fuzzy.filterExact<T>(v);
            });
        }
        printf(GRN "  ✓ %s → %s 条\n" RST, fmtN(before).c_str(), fmtN(fuzzy.size()).c_str());
        if (fuzzy.size() <= 20 && fuzzy.size() > 0) showResults({"l"});
    }

    // ── 显示 ──
    void showResults(const std::vector<std::string>& parts) {
        size_t n = fuzzy.size();
        if (n == 0) { printf("  (无结果)\n"); return; }
        int show = 20;
        if (parts.size() >= 2) parse(parts[1], show);
        show = std::min(show, (int)n);

        printf("  %-4s %-18s %s\n", "序号", "地址", "值");
        for (int i = 0; i < show; i++) {
            printf("  " BLD "%-4d" RST " 0x%016llx  ", i + 1,
                   (unsigned long long)addrAt(i));
            printVal(i);
            printf("\n");
        }
        if (n > (size_t)show) printf(GRY "  ... (%s more)\n" RST, fmtN(n - show).c_str());
    }

    // ── 修改 ──
    void modify(const std::vector<std::string>& parts) {
        if (fuzzy.size() == 0) { printf(RED "  无结果\n" RST); return; }
        if (parts.size() >= 3 && parts[1] == "a") {
            // ma <value>
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(parts[2], v)) { printf(RED "  无效值\n" RST); return; }
                for (size_t i = 0; i < fuzzy.size(); i++) fuzzy.setValueAt<T>(i, v);
            });
            printf(GRN "  ✓ 已修改 %s 条 (w=写回)\n" RST, fmtN(fuzzy.size()).c_str());
        } else if (parts.size() >= 3) {
            int idx; if (!parse(parts[1], idx) || idx < 1 || (size_t)idx > fuzzy.size())
            { printf(RED "  无效索引\n" RST); return; }
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(parts[2], v)) { printf(RED "  无效值\n" RST); return; }
                fuzzy.setValueAt<T>(idx - 1, v);
            });
            printf(GRN "  ✓ [%d] 已修改 (w=写回)\n" RST, idx);
        } else {
            printf("  m <index> <value> | ma <value>\n");
        }
    }

    void writeBack() {
        if (fuzzy.size() == 0) { printf(RED "  无结果\n" RST); return; }
        callTyped([&](auto dummy) { fuzzy.writeBack<decltype(dummy)>(); });
        printf(GRN "  ✓ 已写回\n" RST);
    }

private:
    // ── 类型分发 ──
    template <typename F>
    void callTyped(F&& fn) {
        switch (vtype) {
            case VType::DWORD:  { int32_t d{}; fn(d); break; }
            case VType::FLOAT:  { float f{}; fn(f); break; }
            case VType::QWORD:  { int64_t q{}; fn(q); break; }
            case VType::DOUBLE: { double d{}; fn(d); break; }
        }
    }

    // ── 结果存储 ──
    template <typename T>
    void storeResults(const ResultSet<T>& rs) {
        fuzzy.m_cfg.maxIndividual = 200000000;
        // 使用 searchValue 的等价逻辑
        if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                      std::is_same_v<T, float> || std::is_same_v<T, double>) {
            fuzzy.searchValue<T>(params, rs.size() > 0 ? rs[0].value : T{}, nullptr);
            // 注: 这是简化的, >N/<N 搜索暂不走 FuzzySearch 快照路径
        }
    }

    uintptr_t addrAt(size_t i) {
        switch (vtype) {
            case VType::DWORD: return fuzzy.addrAt<int32_t>(i);
            case VType::FLOAT: return fuzzy.addrAt<float>(i);
            case VType::QWORD: return fuzzy.addrAt<int64_t>(i);
            case VType::DOUBLE: return fuzzy.addrAt<double>(i);
        }
        return 0;
    }

    void printVal(size_t i) {
        switch (vtype) {
            case VType::DWORD: printf("%d", fuzzy.valueAt<int32_t>(i)); break;
            case VType::FLOAT: printf("%.6f", fuzzy.valueAt<float>(i)); break;
            case VType::QWORD: printf("%lld", (long long)fuzzy.valueAt<int64_t>(i)); break;
            case VType::DOUBLE: printf("%.12f", fuzzy.valueAt<double>(i)); break;
        }
    }
};

// ==================== 主函数 ====================
int main() {
    printf(BLD "\n╔══════════════════════════════════════╗\n" RST);
    printf(BLD "║  " CYN "GameGuardian-style 修改器 v1.0" RST BLD "    ║\n" RST);
    printf(BLD "╚══════════════════════════════════════╝\n\n" RST);

    // 进程选择
    auto procs = Process::list_processes();
    if (procs.empty()) { printf(RED "无进程\n" RST); return 1; }

    printf(CYN "  选择目标进程:\n\n" RST);
    std::sort(procs.begin(), procs.end(), [](auto& a, auto& b) { return a.name < b.name; });
    for (size_t i = 0; i < procs.size() && i < 50; i++)
        printf("  " BLD "%2zu" RST ". PID %-6d %s\n", i + 1, procs[i].pid, procs[i].name.c_str());

    printf(GRY "\n  输入: 编号 | PID | 包名\n  > " RST);
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty() || line == "q") return 0;

    int pid = 0;
    // 编号
    size_t idx; if (parse(line, idx) && idx >= 1 && idx <= procs.size()) pid = procs[idx - 1].pid;
    // PID
    if (pid <= 0) pid = atoi(line.c_str());
    // 包名
    if (pid <= 0) {
        pid = Process::get_pid_by_name(line.c_str());
        if (pid <= 0) for (auto& p : procs)
            if (p.name.find(line) != std::string::npos) { pid = p.pid; break; }
    }
    if (pid <= 0) { printf(RED "  未找到\n" RST); return 1; }

    printf(GRN "  PID: %d\n" RST, pid);

    // 映射内存
    printf("  映射..."); fflush(stdout);
    MemMmap::Config cfg; cfg.mapTypeMask = MemType::RANGE_ALL; cfg.maxMappedMB = 4096;
    MemMmap mem(pid, cfg);
    mem.mapRegions();
    printf(" ✓ %s\n\n", fmtS(mem.mappedSize()).c_str());

    printf("  " GRY "快速开始: t d → r Ca → s 100 → (改值) → f + → l → m 1 999 → w\n" RST);
    printf("  " GRY "输入 ? 查看全部命令\n\n" RST);

    GGTool tool(mem);
    tool.run();
    return 0;
}
