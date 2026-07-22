/**
 * GameGuardian-style 控制台修改器 v2.0
 * 整合 MemorySearch 全部 API 功能展示
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
#include <chrono>
#include <iomanip>
#include <fstream>

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
static std::string fmtTime(double ms) {
    if (ms < 1000) { char b[32]; snprintf(b, 32, "%.0fms", ms); return b; }
    char b[32]; snprintf(b, 32, "%.2fs", ms / 1000); return b;
}
static bool bar(double p) {
    int w = 30, f = (int)(p * w);
    printf("\r  ["); for (int i = 0; i < w; i++) printf("%s", i < f ? "=" : " ");
    printf("] %3.0f%%", p * 100); fflush(stdout); return true;
}

// ==================== 类型 ====================
enum class VType { DWORD, FLOAT, QWORD, DOUBLE };
static const char* tname(VType t) {
    switch (t) { case VType::DWORD: return "DWORD"; case VType::FLOAT: return "FLOAT";
                 case VType::QWORD: return "QWORD"; case VType::DOUBLE: return "DOUBLE"; }
    return "?";
}

// ==================== 区域 ====================
static const char* rname(uint32_t m) {
    if (m == MemType::RANGE_ALL)       return "ALL";
    if (m == MemType::RANGE_RW)        return "A:AnonRW";
    if (m == MemType::RANGE_JAVA_HEAP) return "Jh:JavaHeap";
    if (m == MemType::RANGE_C_ALLOC)   return "Ca:CAlloc";
    if (m == MemType::RANGE_C_DATA)    return "Cd:CData";
    if (m == MemType::RANGE_ANONYMOUS) return "Anon";
    if (m == MemType::RANGE_CODE_APP)  return "Xa:CodeApp";
    return "?";
}

// ==================== 主逻辑 ====================
class GGTool {
public:
    GGTool(MemBase& m) : mem(memRef), fuzzy(m), search(m), memRef(m) {
        sp.chunkSize = 32 * 1024 * 1024;
        sp.memTypeMask = MemType::RANGE_ALL;
    }

    void run() {
        printf(BLD CYN "\n  ▸ MemorySearch 全功能控制台\n" RST);
        printf(GRY "  ?=帮助  s=搜索  f=精炼  l=列出  w=写回  demo=演示  q=退出\n\n" RST);

        while (true) {
            showBar();
            printf(CYN "  > " RST);
            std::string line;
            std::getline(std::cin, line);
            if (!std::cin) break;
            auto parts = split(line);
            if (parts.empty()) continue;
            std::string cmd = parts[0];

            if (cmd == "q") break;
            else if (cmd == "?" || cmd == "h") help();
            else if (cmd == "demo") runDemo();
            else if (cmd == "t") setType(parts);
            else if (cmd == "r") setRegion(parts);
            else if (cmd == "s") searchExact(parts);
            else if (cmd == "u") searchUnknown(parts);
            else if (cmd == "sc") searchCompare(parts);
            else if (cmd == "sr") searchRange(parts);
            else if (cmd == "p") scanPattern(parts);
            else if (cmd == "str") searchStr(parts);
            else if (cmd == "f") refine(parts);
            else if (cmd == "l") showResults(parts);
            else if (cmd == "w") cmdWrite(parts);
            else if (cmd == "c") { fuzzy.reset(); tmpAddrs.clear(); tmpVals.clear(); printf(GRN "  已清空\n" RST); }
            else if (cmd == "v") verifyAddr(parts);
            else if (cmd == "dump") dumpMem(parts);
            else if (cmd == "map") showMap();
            else if (cmd == "base") showBase(parts);
            else if (cmd == "stats") showStats();
            else if (cmd == "e") cmdExport(parts);
            else if (cmd == "offset") filterOffset(parts);
            else if (cmd == "mem") { printf("  内存: %s\n", fmtS(fuzzy.memoryUsed()).c_str()); }
            else printf(RED "  ? 未知, 输入 ? 查看帮助\n" RST);
        }
    }

private:
    MemBase& memRef;
    FuzzySearch fuzzy;
    SearchEngine search;
    MemBase& mem;  // 别名, 用于直接读写
    SearchParams sp;
    VType vtype = VType::DWORD;

    std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> r; std::stringstream ss(s); std::string t;
        while (ss >> t) r.push_back(t); return r;
    }

    size_t totalSize() const { return fuzzy.size() > 0 ? fuzzy.size() : tmpAddrs.size(); }
    void showBar() {
        size_t n = totalSize();
        printf(GRY "  [%s|%s] " RST "%s" RST "\n",
               tname(vtype), rname(sp.memTypeMask),
               n > 0 ? (BLD + fmtN(n)).c_str() : "0");
    }

    void help() {
        printf(BLD "\n  ═══ MemorySearch 全功能控制台 ═══\n\n" RST);
        printf("  " CYN "── 设置 ──\n" RST);
        printf("  t d|f|q|dd          类型 DWORD/FLOAT/QWORD/DOUBLE\n");
        printf("  r A|Ca|Jh|Cd|Anon|Xa 内存区域\n");
        printf("  map                  内存映射概览\n");
        printf("  base <module>        模块基址\n");
        printf("\n  " CYN "── 搜索 ──\n" RST);
        printf("  s <value>            精确值搜索 (DWORD/FLOAT/QWORD)\n");
        printf("  s >N / s <N          大于/小于搜索\n");
        printf("  sc <op> <value>      比较搜索 (GT/LT/NEQ/GTE/LTE)\n");
        printf("  sr <min> <max>       范围搜索\n");
        printf("  p <hex>              特征码扫描 (如 \"12 ?? 34\")\n");
        printf("  str <text>           UTF-8 字符串搜索\n");
        printf("  u                    未知值搜索 (自动快照)\n");
        printf("\n  " CYN "── 精炼 ──\n" RST);
        printf("  f +|-|~|=            变大/变小/变化/未变 (需s或u后)\n");
        printf("  f <value>            精炼为精确值\n");
        printf("  f >N / f <N          精炼为大于/小于\n");
        printf("  offset <N> <op> <v>  偏移过滤 (addr+N 处的值)\n");
        printf("\n  " CYN "── 操作 ──\n" RST);
        printf("  l [N]                列出结果\n");
        printf("  w <value>            修改全部并写回\n");
        printf("  w <i> <v>            修改第i个并写回\n");
        printf("  v [N]                验证地址 (当前值 vs 快照)\n");
        printf("  dump <addr> [size]   dump 内存\n");
        printf("  e [file]             导出地址列表\n");
        printf("  c                    清空结果\n");
        printf("\n  " CYN "── 其他 ──\n" RST);
        printf("  stats                最后搜索性能\n");
        printf("  mem                  当前内存用量\n");
        printf("  demo                 运行综合演示\n");
        printf("  q                    退出\n\n");
    }

    // ==================== 设置 ====================
    void setType(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  d|f|q|dd\n"); return; }
        auto t = parts[1]; fuzzy.reset();
        if (t == "d") vtype = VType::DWORD;
        else if (t == "f") vtype = VType::FLOAT;
        else if (t == "q") vtype = VType::QWORD;
        else if (t == "dd") vtype = VType::DOUBLE;
        else { printf(RED "  d|f|q|dd\n" RST); return; }
        printf(GRN "  ✓ %s\n" RST, tname(vtype));
    }

    void setRegion(const std::vector<std::string>& parts) {
        if (parts.size() < 2) {
            auto maps = Process::get_process_maps(mem.get_pid());
            for (auto& m : maps) if (m.isValid() && m.readable)
                printf("  %-4s %s\n", rname(m.getMemType()), fmtS(m.length).c_str());
            printf("  ALL  (全部)\n"); return;
        }
        auto t = parts[1]; fuzzy.reset();
        if (t == "ALL") sp.memTypeMask = MemType::RANGE_ALL;
        else if (t == "A") sp.memTypeMask = MemType::RANGE_RW;
        else if (t == "Jh") sp.memTypeMask = MemType::RANGE_JAVA_HEAP;
        else if (t == "Ca") sp.memTypeMask = MemType::RANGE_C_ALLOC;
        else if (t == "Cd") sp.memTypeMask = MemType::RANGE_C_DATA;
        else if (t == "Anon") sp.memTypeMask = MemType::RANGE_ANONYMOUS;
        else if (t == "Xa") sp.memTypeMask = MemType::RANGE_CODE_APP;
        else { printf(RED "  无效\n" RST); return; }
        printf(GRN "  ✓ %s\n" RST, rname(sp.memTypeMask));
    }

    // ==================== 搜索 ====================
    void searchExact(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  s <value> | s >N | s <N\n"); return; }
        fuzzy.reset(); tmpAddrs.clear(); tmpVals.clear();
        auto op = parts[1];

        // 大于/小于
        if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
            bool gt = op[0] == '>';
            std::string vs = op.substr(1);
            callTyped([&](auto dummy) {
                using T = decltype(dummy);
                T v; if (!parse(vs, v)) { printf(RED "  无效值\n" RST); return; }
                auto t0 = std::chrono::high_resolution_clock::now();
                auto r = search.searchCompare<T>(sp, v, gt ? CompareOp::GT : CompareOp::LT, bar);
                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                printf("\r" GRN "  ✓ %s 结果 | %s\n" RST, fmtN(r.size()).c_str(), fmtTime(ms).c_str());
                storeResults(r);
            });
            return;
        }

        // 精确值
        callTyped([&](auto dummy) {
            using T = decltype(dummy);
            T v; if (!parse(op, v)) { printf(RED "  无效值\n" RST); return; }
            auto t0 = std::chrono::high_resolution_clock::now();
            fuzzy.searchValue<T>(sp, v, bar);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            printf("\r" GRN "  ✓ %s 条 | %s | 快照已创建\n" RST,
                   fmtN(totalSize()).c_str(), fmtTime(ms).c_str());
        });
    }

    void searchUnknown(const std::vector<std::string>&) {
        fuzzy.reset(); tmpAddrs.clear(); tmpVals.clear();
        auto t0 = std::chrono::high_resolution_clock::now();
        callTyped([&](auto dummy) { fuzzy.searchUnknown<decltype(dummy)>(sp, 0, bar); });
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (fuzzy.phase() == FuzzySearch::Phase::REGION_SNAPSHOT)
            printf("\r" GRN "  ✓ 区域快照 %s | %s\n" RST,
                   fmtS(fuzzy.memoryUsed()).c_str(), fmtTime(ms).c_str());
        else
            printf("\r" GRN "  ✓ %s 条 | %s | 快照已创建\n" RST,
                   fmtN(totalSize()).c_str(), fmtTime(ms).c_str());
    }

    void searchCompare(const std::vector<std::string>& parts) {
        if (parts.size() < 3) { printf("  sc GT|LT|NEQ|GTE|LTE <value>\n"); return; }
        CompareOp op;
        std::string os = parts[1];
        if (os == "GT") op = CompareOp::GT; else if (os == "LT") op = CompareOp::LT;
        else if (os == "NEQ") op = CompareOp::NEQ; else if (os == "GTE") op = CompareOp::GTE;
        else if (os == "LTE") op = CompareOp::LTE;
        else { printf(RED "  GT|LT|NEQ|GTE|LTE\n" RST); return; }

        callTyped([&](auto dummy) {
            using T = decltype(dummy);
            T v; if (!parse(parts[2], v)) { printf(RED "  无效值\n" RST); return; }
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = search.searchCompare<T>(sp, v, op, bar);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            printf("\r" GRN "  ✓ %s 结果 | %s\n" RST, fmtN(r.size()).c_str(), fmtTime(ms).c_str());
            storeResults(r);
        });
    }

    void searchRange(const std::vector<std::string>& parts) {
        if (parts.size() < 3) { printf("  sr <min> <max>\n"); return; }
        callTyped([&](auto dummy) {
            using T = decltype(dummy);
            T min, max; if (!parse(parts[1], min) || !parse(parts[2], max))
            { printf(RED "  无效\n" RST); return; }
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = search.searchRange<T>(sp, min, max, bar);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            printf("\r" GRN "  ✓ %s 结果 | %s\n" RST, fmtN(r.size()).c_str(), fmtTime(ms).c_str());
            storeResults(r);
        });
    }

    void scanPattern(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  p <hex> (如 \"12 ?? 34\")\n"); return; }
        auto t0 = std::chrono::high_resolution_clock::now();
        auto results = search.scanPatternString(sp, parts[1]);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf(GRN "  ✓ %s 匹配 | %s\n" RST, fmtN(results.size()).c_str(), fmtTime(ms).c_str());
        // 显示前10个
        for (size_t i = 0; i < std::min(results.size(), (size_t)10); i++)
            printf("    0x%llx\n", (unsigned long long)results[i]);
    }

    void searchStr(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  str <text>\n"); return; }
        auto t0 = std::chrono::high_resolution_clock::now();
        auto results = search.searchString(sp, parts[1], true, true);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf(GRN "  ✓ %s 匹配 | %s\n" RST, fmtN(results.size()).c_str(), fmtTime(ms).c_str());
        for (size_t i = 0; i < std::min(results.size(), (size_t)10); i++)
            printf("    0x%llx\n", (unsigned long long)results[i]);
    }

    // ==================== 精炼 ====================
    void refine(const std::vector<std::string>& parts) {
        size_t n = totalSize();
        if (n == 0 && fuzzy.phase() != FuzzySearch::Phase::REGION_SNAPSHOT) {
            printf(RED "  先搜索 (s/u)\n" RST); return;
        }
        if (parts.size() < 2) { printf("  f +|-|~|=|<val>|>N|<N\n"); return; }
        auto op = parts[1];
        size_t before = n;
        if (fuzzy.phase() == FuzzySearch::Phase::REGION_SNAPSHOT && before == 0)
            before = fuzzy.stats().phase1Results;

        bool useTmp = (fuzzy.size() == 0 && !tmpAddrs.empty());
        auto t0 = std::chrono::high_resolution_clock::now();

        if (useTmp) {
            // 在 tmp 结果上直接过滤: 读实时值 → 比较
            std::vector<uintptr_t> newA; std::vector<int64_t> newV;
            callTyped([&](auto d) {
                using T = decltype(d);
                for (size_t i = 0; i < tmpAddrs.size(); i++) {
                    T cur{}; mem.read(tmpAddrs[i], &cur, sizeof(T));
                    bool keep = false;
                    if (op == "+" || op == "-" || op == "~" || op == "=") {
                        T old; memcpy(&old, &tmpVals[i], sizeof(T));
                        keep = (op=="+") ? cur>old : (op=="-") ? cur<old :
                               (op=="~") ? cur!=old : cur==old;
                    } else if (op.size()>=2 && op[0]=='>') {
                        T ref; std::string vs=op.substr(1); if(parse(vs,ref)) keep=cur>ref;
                    } else if (op.size()>=2 && op[0]=='<') {
                        T ref; std::string vs=op.substr(1); if(parse(vs,ref)) keep=cur<ref;
                    } else {
                        T ref; if (parse(op, ref)) keep = (cur == ref);
                    }
                    if (keep) { newA.push_back(tmpAddrs[i]); newV.push_back((int64_t)cur); }
                }
            });
            tmpAddrs.swap(newA); tmpVals.swap(newV);
        } else if (fuzzy.phase() == FuzzySearch::Phase::REGION_SNAPSHOT) {
            // 区域快照模式: 直接搜索, 不走filterCompare (BulkResults为空)
            if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
                std::string vs = op.substr(1);
                callTyped([&](auto d) {
                    using T = decltype(d); T v; parse(vs, v);
                    auto r = search.searchCompare<T>(sp, v,
                        op[0]=='>'?CompareOp::GT:CompareOp::LT, bar);
                    printf("\r"); storeResults(r);
                });
            } else {
                callTyped([&](auto d) {
                    using T = decltype(d); T v; parse(op, v);
                    auto r = search.search<T>(sp, v, bar);
                    printf("\r"); storeResults(r);
                });
            }
        } else if (op == "+" || op == "-" || op == "~" || op == "=") {
            CompareOp cop = op == "+" ? CompareOp::INCREASED : op == "-" ? CompareOp::DECREASED
                          : op == "~" ? CompareOp::CHANGED : CompareOp::UNCHANGED;
            callTyped([&](auto d) { fuzzy.refine<decltype(d)>(cop); });
        } else if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
            std::string vs = op.substr(1);
            callTyped([&](auto d) {
                using T = decltype(d); T v; if (parse(vs, v))
                { fuzzy.filterCompare<T>(op[0]=='>'?CompareOp::GT:CompareOp::LT, v); }
            });
        } else {
            callTyped([&](auto d) {
                using T = decltype(d); T v; if (parse(op, v)) fuzzy.filterExact<T>(v);
            });
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf(GRN "  ✓ %s → %s 条 | %s\n" RST,
               fmtN(before).c_str(), fmtN(totalSize()).c_str(), fmtTime(ms).c_str());
        if (totalSize() <= 20 && totalSize() > 0) showResults({"l"});
    }

    void filterOffset(const std::vector<std::string>& parts) {
        if (totalSize() == 0) { printf(RED "  先搜索\n" RST); return; }
        if (parts.size() < 4) { printf("  offset <N> EQ|GT|LT <value>\n"); return; }
        int off; if (!parse(parts[1], off)) { printf(RED "  无效\n" RST); return; }

        size_t before = totalSize(), after = 0;
        callTyped([&](auto d) {
            using T = decltype(d); T v; if (!parse(parts[3], v)) return;
            // 逐地址读 addr+offset, 过滤
            for (size_t i = 0; i < totalSize(); i++) {
                T val = mem.Read<T>(addrAt(i) + off);
                if (val == v) after++;
            }
        });
        printf(GRN "  ✓ %s → %s 条 (offset=%d)\n" RST,
               fmtN(before).c_str(), fmtN(after).c_str(), off);
    }

    // ==================== 显示/操作 ====================
    void showResults(const std::vector<std::string>& parts) {
        size_t n = totalSize();
        if (n == 0) { printf("  (无结果)\n"); return; }
        int show = 20; if (parts.size() >= 2) parse(parts[1], show);
        show = std::min(show, (int)n);
        printf("  %-4s %-18s %s\n", "序号", "地址", "值");
        for (int i = 0; i < show; i++) {
            printf("  " BLD "%-4d" RST " 0x%016llx  ", i + 1, (unsigned long long)addrAt(i));
            printVal(i); printf("\n");
        }
        if (n > (size_t)show) printf(GRY "  ... (%s more)\n" RST, fmtN(n - show).c_str());
    }

    void cmdWrite(const std::vector<std::string>& parts) {
        size_t n = totalSize();
        if (n == 0) { printf(RED "  无结果\n" RST); return; }
        if (parts.size() < 2) { printf("  w <value> | w <index> <value>\n"); return; }
        bool useTmp = (fuzzy.size() == 0 && !tmpAddrs.empty());

        if (parts.size() >= 3) {
            int idx; if (!parse(parts[1], idx) || idx < 1 || (size_t)idx > n)
            { printf(RED "  无效索引\n" RST); return; }
            size_t i = idx - 1;
            if (useTmp) {
                callTyped([&](auto d) {
                    using T = decltype(d); T v; if (!parse(parts[2], v)) return;
                    mem.Write<T>(tmpAddrs[i], v);
                });
            } else {
                callTyped([&](auto d) {
                    using T = decltype(d); T v; if (!parse(parts[2], v)) return;
                    fuzzy.setValueAt<T>(i, v); fuzzy.writeBack<T>();
                });
            }
            printf(GRN "  ✓ [%d] 已修改并写回\n" RST, idx);
        } else {
            callTyped([&](auto d) {
                using T = decltype(d); T v; if (!parse(parts[1], v)) return;
                if (useTmp)
                    for (size_t i = 0; i < n; i++) mem.Write<T>(tmpAddrs[i], v);
                else {
                    for (size_t i = 0; i < n; i++) fuzzy.setValueAt<T>(i, v);
                    fuzzy.writeBack<T>();
                }
            });
            printf(GRN "  ✓ 已修改 %s 条并写回\n" RST, fmtN(n).c_str());
        }
    }

    void verifyAddr(const std::vector<std::string>& parts) {
        if (totalSize() == 0) { printf("  (无结果)\n"); return; }
        int n = 5; if (parts.size() >= 2) parse(parts[1], n);
        n = std::min(n, (int)totalSize());
        printf("  当前值 vs 快照 (前%d):\n", n);
        for (int i = 0; i < n; i++) {
            uintptr_t a = addrAt(i);
            printf("  [%d] 0x%llx  cur=", i+1, (unsigned long long)a);
            callTyped([&](auto d) { using T = decltype(d); T v{}; mem.read(a, &v, sizeof(T)); printOne(v); });
            printf("  snap="); printVal(i); printf("\n");
        }
    }

    void dumpMem(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  dump <addr> [size]\n"); return; }
        uintptr_t addr; size_t sz = 64;
        if (!parse(parts[1], addr)) { printf(RED "  无效地址\n" RST); return; }
        if (parts.size() >= 3) parse(parts[2], sz);
        std::vector<uint8_t> buf(sz);
        if (mem.read(addr, buf.data(), sz)) {
            for (size_t i = 0; i < sz; i++) {
                if (i % 16 == 0) printf("\n  0x%llx: ", (unsigned long long)(addr + i));
                printf("%02X ", buf[i]);
            }
            printf("\n");
        } else printf(RED "  读取失败\n" RST);
    }

    void showMap() {
        auto maps = Process::get_process_maps(mem.get_pid());
        printf("  %-14s %10s %s\n", "类型", "大小", "路径");
        for (auto& m : maps)
            if (m.isValid() && m.readable)
                printf("  %-14s %10s %s\n", rname(m.getMemType()), fmtS(m.length).c_str(),
                       m.pathname.empty() ? "(anon)" : m.pathname.c_str());
    }

    void showBase(const std::vector<std::string>& parts) {
        if (parts.size() < 2) { printf("  base <module>\n"); return; }
        auto base = mem.get_module_base(parts[1].c_str());
        auto end  = mem.get_module_end(parts[1].c_str());
        if (base) printf("  基址: 0x%llx  结束: 0x%llx  大小: %s\n",
                         (unsigned long long)base, (unsigned long long)end, fmtS(end-base).c_str());
        else printf(RED "  未找到模块 %s\n" RST, parts[1].c_str());
    }

    void showStats() { search.lastStats().print(); }

    void cmdExport(const std::vector<std::string>& parts) {
        if (totalSize() == 0) { printf("  (无结果)\n"); return; }
        std::string fn = "addrs.txt"; if (parts.size() >= 2) fn = parts[1];
        std::ofstream f(fn); if (!f) { printf(RED "  无法创建\n" RST); return; }
        for (size_t i = 0; i < totalSize(); i++) f << "0x" << std::hex << addrAt(i) << std::dec << "\n";
        printf(GRN "  ✓ %s 地址 → %s\n" RST, fmtN(totalSize()).c_str(), fn.c_str());
    }

    // ==================== 演示 ====================
    void runDemo() {
        printf(BLD CYN "\n  ═══ MemorySearch 综合演示 ═══\n\n" RST);

        // 1. 精确值
        printf(GRY "  [1/7] 精确值搜索 int=100\n" RST);
        { auto t0 = now(); auto r = search.search<int>(sp, 100);
            printf("    ✓ %s 结果 | %s\n", fmtN(r.size()).c_str(), fmtTime(now()-t0).c_str()); }

        // 2. 模糊搜索
        printf(GRY "  [2/7] 模糊搜索 int>999999\n" RST);
        { auto t0 = now(); auto r = search.searchCompare<int>(sp, 999999, CompareOp::GT, bar);
            printf("\r    ✓ %zu 结果 | %s\n", r.size(), fmtTime(now()-t0).c_str()); }

        // 3. 模式扫描
        printf(GRY "  [3/7] 模式扫描 \"1F 20 03 D5\" (ARM64 NOP)\n" RST);
        { auto t0 = now(); auto r = search.scanPatternString(sp, "1F 20 03 D5");
            printf("    ✓ %zu 匹配 | %s\n", r.size(), fmtTime(now()-t0).c_str()); }

        // 4. 字符串
        printf(GRY "  [4/7] UTF-8 搜索 \"Unity\"\n" RST);
        { auto t0 = now(); auto r = search.searchString(sp, "Unity", true, true);
            printf("    ✓ %zu 匹配 | %s\n", r.size(), fmtTime(now()-t0).c_str()); }

        // 5. 范围搜索
        printf(GRY "  [5/7] 范围搜索 float [0.9, 1.1]\n" RST);
        { auto t0 = now(); auto r = search.searchRange<float>(sp, 0.9f, 1.1f, bar);
            printf("\r    ✓ %zu 结果 | %s\n", r.size(), fmtTime(now()-t0).c_str()); }

        // 6. 模糊搜索流程
        printf(GRY "  [6/7] 模糊搜索: s 100 → f =\n" RST);
        { fuzzy.searchValue<int>(sp, 100, bar); printf("\r");
            size_t b = totalSize(); fuzzy.refine<int>(CompareOp::UNCHANGED);
            printf("    s 100: %s 条 → f =: %s 条\n", fmtN(b).c_str(), fmtN(totalSize()).c_str()); }

        // 7. 性能
        printf(GRY "  [7/7] 性能统计\n" RST);
        search.lastStats().print();

        printf(BLD CYN "\n  ═══ 演示完成 ═══\n\n" RST);
        fuzzy.reset();
    }

    double now() {
        auto t = std::chrono::high_resolution_clock::now();
        static auto base = t;
        return std::chrono::duration<double, std::milli>(t - base).count();
    }

private:
    // ==================== 类型分发 ====================
    template <typename F> void callTyped(F&& fn) {
        switch (vtype) { case VType::DWORD: { int32_t d{}; fn(d); break; }
                         case VType::FLOAT: { float f{}; fn(f); break; }
                         case VType::QWORD: { int64_t q{}; fn(q); break; }
                         case VType::DOUBLE: { double d{}; fn(d); break; } }
    }

    // ── 比较/范围搜索结果存储 (不截断, 全部保存) ──
    template <typename T>
    void storeResults(const ResultSet<T>& rs) {
        fuzzy.reset();
        tmpAddrs.clear(); tmpVals.clear();
        size_t n = rs.size();
        size_t memEst = n * (sizeof(uintptr_t) + sizeof(T));
        if (memEst > 500 * 1024 * 1024)
            printf(YEL "  ⚠ %s 条, 预计内存 %s\n" RST, fmtN(n).c_str(), fmtS(memEst).c_str());
        tmpAddrs.reserve(n);
        tmpVals.reserve(n);
        for (size_t i = 0; i < n; i++) {
            tmpAddrs.push_back(rs[i].address);
            tmpVals.push_back(rs[i].value);
        }
    }
    std::vector<uintptr_t> tmpAddrs;
    std::vector<int64_t> tmpVals;

    uintptr_t addrAt(size_t i) {
        if (fuzzy.size() > 0) {
            switch (vtype) { case VType::DWORD: return fuzzy.addrAt<int32_t>(i);
                             case VType::FLOAT: return fuzzy.addrAt<float>(i);
                             case VType::QWORD: return fuzzy.addrAt<int64_t>(i);
                             case VType::DOUBLE: return fuzzy.addrAt<double>(i); }
        }
        return i < tmpAddrs.size() ? tmpAddrs[i] : 0;
    }
    void printVal(size_t i) {
        if (fuzzy.size() > 0) {
            switch (vtype) { case VType::DWORD: printf("%d", fuzzy.valueAt<int32_t>(i)); break;
                             case VType::FLOAT: printf("%.6f", fuzzy.valueAt<float>(i)); break;
                             case VType::QWORD: printf("%lld", (long long)fuzzy.valueAt<int64_t>(i)); break;
                             case VType::DOUBLE: printf("%.12f", fuzzy.valueAt<double>(i)); break; }
            return;
        }
        if (i < tmpVals.size()) {
            switch (vtype) { case VType::DWORD: printf("%d", (int32_t)tmpVals[i]); break;
                             case VType::FLOAT: { float f; int32_t v=(int32_t)tmpVals[i]; memcpy(&f,&v,4); printf("%.6f",f); break; }
                             case VType::QWORD: printf("%lld", (long long)tmpVals[i]); break;
                             case VType::DOUBLE: { double d; memcpy(&d,&tmpVals[i],8); printf("%.12f",d); break; } }
        }
    }
    template <typename T> void printOne(T v) { printf("%d", (int)v); }
    void printOne(float v) { printf("%.6f", v); }
    void printOne(double v) { printf("%.12f", v); }
};

// ==================== 主函数 ====================
int main() {
    printf(BLD "\n╔══════════════════════════════════════╗\n" RST);
    printf(BLD "║  " CYN "MemorySearch 全功能控制台 v2.0" RST BLD "    ║\n" RST);
    printf(BLD "╚══════════════════════════════════════╝\n\n" RST);

    auto procs = Process::list_processes();
    if (procs.empty()) { printf(RED "无进程\n" RST); return 1; }
    printf(CYN "  选择目标进程:\n\n" RST);
    std::sort(procs.begin(), procs.end(), [](auto& a, auto& b) { return a.name < b.name; });
    for (size_t i = 0; i < procs.size() && i < 50; i++)
        printf("  " BLD "%2zu" RST ". PID %-6d %s\n", i+1, procs[i].pid, procs[i].name.c_str());

    printf(GRY "\n  输入: 编号 | PID | 包名\n  > " RST);
    std::string line; std::getline(std::cin, line); line = trim(line);
    if (line.empty() || line == "q") return 0;

    int pid = 0; size_t idx;
    if (parse(line, idx) && idx >= 1 && idx <= procs.size()) pid = procs[idx-1].pid;
    if (pid <= 0) pid = atoi(line.c_str());
    if (pid <= 0) { pid = Process::get_pid_by_name(line.c_str());
        if (pid <= 0) for (auto& p : procs) if (p.name.find(line) != std::string::npos) { pid = p.pid; break; } }
    if (pid <= 0) { printf(RED "  未找到\n" RST); return 1; }
    printf(GRN "  PID: %d\n" RST, pid);

    printf("  映射..."); fflush(stdout);
    MemMmap::Config cfg; cfg.mapTypeMask = MemType::RANGE_ALL; cfg.maxMappedMB = 4096;
    MemMmap mmapMem(pid, cfg); mmapMem.mapRegions();
    printf(" ✓ %s\n\n", fmtS(mmapMem.mappedSize()).c_str());

    printf(GRY "  输入 demo 运行综合演示, ? 查看全部命令\n\n" RST);
    GGTool tool(mmapMem); tool.run();
    return 0;
}
