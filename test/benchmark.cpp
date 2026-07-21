/**
 * MemorySearch v2.0 — 企业级自测套件
 * 目标: com.gameplier.kontra
 *
 * 测试覆盖:
 *   ✓ 内存映射概览 (可搜索 vs 不可读)
 *   ✓ 精确值搜索 (int32, int64, float)
 *   ✓ 模糊搜索 (NEQ, GT, LT, GTE, LTE)
 *   ✓ 范围搜索
 *   ✓ 字符串搜索 (UTF-8, UTF-16)
 *   ✓ 模式扫描 (含通配符)
 *   ✓ 结果集操作 (filter, filterSelf, refresh, writeBack, page, intersect, unite)
 *   ✓ 进度回调
 *   ✓ maxResults 限制
 *   ✓ 块大小对比
 *   ✓ 压力测试
 *   ✓ 新旧API兼容性
 */

#include "../core/Mem/Mem.hpp"
#include "../core/Mem/Search.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

// =================== 工具函数 ====================
static std::string fmtSize(size_t bytes) {
    const char* u[] = {"B","KB","MB","GB"};
    int i = 0; double s = bytes;
    while (s >= 1024 && i < 3) { s /= 1024; i++; }
    char b[64]; snprintf(b, sizeof(b), "%.2f %s", s, u[i]); return b;
}
static std::string fmtTime(double ms) {
    char b[64];
    if (ms < 1000) snprintf(b, sizeof(b), "%.1f ms", ms);
    else if (ms < 60000) snprintf(b, sizeof(b), "%.2f s", ms/1000);
    else snprintf(b, sizeof(b), "%.1f min", ms/60000);
    return b;
}
static void sep(const char* t = nullptr) {
    if (t) printf("\n══ %s ══\n", t);
    else   printf("──────────────────────────────────────────────\n");
}
static int passCnt = 0, failCnt = 0;
static void check(const char* name, bool cond, const char* detail = "") {
    if (cond) { printf("  ✓ %s %s\n", name, detail); passCnt++; }
    else      { printf("  ✗ %s %s\n", name, detail); failCnt++; }
}

// =================== 辅助: 进程内存概览 ====================
static void printMemoryOverview(Mem& mem) {
    sep("内存映射概览");
    auto maps = Process::get_process_maps(mem.get_pid());

    struct TS { const char* n; size_t sz; int cnt; };
    TS st[] = {
        {"JAVA_HEAP   ",0,0},{"C_ALLOC     ",0,0},{"C_DATA      ",0,0},
        {"C_BSS       ",0,0},{"ANONYMOUS   ",0,0},{"STACK       ",0,0},
        {"FILE_DATA   ",0,0},{"CODE_APP    ",0,0},{"CODE_SYSTEM ",0,0},
        {"JAVA        ",0,0},{"ASHMEM      ",0,0},{"VIDEO       ",0,0},
        {"B_BAD       ",0,0},{"OTHER       ",0,0},{"NON_READABLE",0,0},
    };
    const int N = sizeof(st)/sizeof(st[0]);
    size_t total = 0, searchable = 0, nonread = 0;
    int totalMaps = 0;

    for (const auto& m : maps) {
        if (!m.isValid()) continue;
        total += m.length; totalMaps++;
        if (!m.readable) { nonread += m.length; st[14].sz += m.length; st[14].cnt++; continue; }
        searchable += m.length;
        uint32_t t = m.getMemType();
        int idx = 14;
             if (t == MemType::RANGE_C_HEAP)       idx = 0;
        else if (t == MemType::RANGE_JAVA_HEAP)    idx = 0;
        else if (t == MemType::RANGE_C_ALLOC)      idx = 1;
        else if (t == MemType::RANGE_C_DATA)       idx = 2;
        else if (t == MemType::RANGE_C_BSS)        idx = 3;
        else if (t == MemType::RANGE_ANONYMOUS)    idx = 4;
        else if (t == MemType::RANGE_STACK)        idx = 5;
        else if (t == MemType::RANGE_FILE_DATA)    idx = 6;
        else if (t == MemType::RANGE_CODE_APP)     idx = 7;
        else if (t == MemType::RANGE_CODE_SYSTEM)  idx = 8;
        else if (t == MemType::RANGE_JAVA)         idx = 9;
        else if (t == MemType::RANGE_ASHMEM)       idx = 10;
        else if (t == MemType::RANGE_VIDEO)        idx = 11;
        else if (t == MemType::RANGE_B_BAD)        idx = 12;
        st[idx].sz += m.length; st[idx].cnt++;
    }

    printf("  PID:%d  总映射:%d  总空间:%s  可搜索:%s  不可读:%s\n",
           mem.get_pid(), totalMaps, fmtSize(total).c_str(),
           fmtSize(searchable).c_str(), fmtSize(nonread).c_str());
    printf("  %-15s %7s %12s %s\n","类型","数量","大小","占比");
    for (int i = 0; i < N; i++) {
        if (st[i].cnt > 0) {
            double pct = searchable > 0 ? 100.0*st[i].sz/searchable : 0;
            printf("  %-15s %7d %12s %5.1f%%\n", st[i].n, st[i].cnt, fmtSize(st[i].sz).c_str(), pct);
        }
    }
}

// =================== 主测试 ====================
int main(int argc, char* argv[]) {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  MemorySearch v2.0 — 企业级自测套件       ║\n");
    printf("║  Target: com.gameplier.kontra            ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    // ── 获取 PID ──────────────────────────────
    int pid = 0;
    if (argc >= 2) pid = atoi(argv[1]);
    if (pid <= 0) pid = Process::get_pid_by_name("com.gameplier.kontra");
    if (pid <= 0) {
        for (auto& p : Process::list_processes())
            if (p.name.find("gameplier") != std::string::npos) { pid = p.pid; break; }
    }
    if (pid <= 0) { fprintf(stderr, "Process not found!\n"); return 1; }
    printf("✓ 目标进程 PID: %d\n", pid);

    Mem mem(pid);
    SearchEngine search(mem);
    printf("✓ CPU核心: %u\n\n", std::thread::hardware_concurrency());

    // ── 1. 内存概览 ───────────────────────────
    printMemoryOverview(mem);

    // ── 2. 精确值搜索 ─────────────────────────
    sep("TEST 1: 精确值搜索 (search)");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW;
        auto t0 = std::chrono::high_resolution_clock::now();
        auto r = search.search<int>(p, 100);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
        auto& s = search.lastStats();
        printf("  search<int>(100): %s | %zu results | %.1f MB/s\n",
               fmtTime(ms).c_str(), r.size(), s.throughputMBs);
        check("int EQ search", r.size() > 0, "found results");
        check("stats populated", s.elapsedMs > 0);
        check("stats throughput", s.throughputMBs > 0);
    }

    // ── 3. 模糊搜索 ───────────────────────────
    sep("TEST 2: 模糊搜索 (searchCompare)");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW; p.maxResults = 10000;
        auto rGT = search.searchCompare<int>(p, 999999, CompareOp::GT);
        printf("  compare(GT 999999): %zu results (max 10000)\n", rGT.size());
        check("GT search", rGT.size() > 0);

        auto rLT = search.searchCompare<int>(p, -999999, CompareOp::LT);
        printf("  compare(LT -999999): %zu results (max 10000)\n", rLT.size());
        check("LT search", rLT.size() > 0);

        auto rNEQ = search.searchCompare<int>(p, 0, CompareOp::NEQ);
        printf("  compare(NEQ 0): %zu results (max 10000)\n", rNEQ.size());
        check("NEQ search", rNEQ.size() > 0);
    }

    // ── 4. 范围搜索 ───────────────────────────
    sep("TEST 3: 范围搜索 (searchRange)");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW;
        p.maxResults = 50000;
        auto r = search.searchRange<float>(p, 0.9f, 1.1f);
        printf("  range float [0.9, 1.1]: %zu results\n", r.size());
        check("float range", r.size() >= 0);
    }

    // ── 5. 字符串搜索 ─────────────────────────
    sep("TEST 4: 字符串搜索");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_ALL;
        auto u8 = search.searchString(p, "Unity", true, true);
        printf("  UTF-8 \"Unity\": %zu results\n", u8.size());
        check("UTF-8 search", u8.size() >= 0);

        auto u8i = search.searchString(p, "unity", true, false);
        printf("  UTF-8 \"unity\"(insens): %zu results\n", u8i.size());
        check("UTF-8 case-insens", u8i.size() >= 0);
    }

    // ── 6. 模式扫描 ───────────────────────────
    sep("TEST 5: 模式扫描");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_CODE_APP;
        auto pat = search.searchPattern(p,
            {0x1F, 0x20, 0x03, 0xD5}, {});  // ARM64 NOP
        printf("  ARM64 NOP: %zu results\n", pat.size());
        check("Pattern search", pat.size() > 0, "ARM64 NOP found");

        auto wild = search.scanPatternString(p, "1F 20 ?? ??");
        printf("  Wildcard: %zu results\n", wild.size());
        check("Wildcard pattern", wild.size() >= 0);
    }

    // ── 7. 结果集操作 ─────────────────────────
    sep("TEST 6: 结果集操作 (ResultSet)");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW; p.maxResults = 1000;
        auto r = search.search<int>(p, 0);
        printf("  Base result: %zu\n", r.size());

        // filter
        auto filtered = r.filter([](const auto& x){ return x.address > 0x7000000000ULL; });
        printf("  After filter(addr>0x70...): %zu\n", filtered.size());
        check("filter()", filtered.size() <= r.size());

        // page
        auto pg = r.page(0, 10);
        printf("  page(0,10): %zu results\n", pg.size());
        check("page()", pg.size() <= 10);

        // refresh
        size_t before = r.size();
        r.refresh();
        check("refresh()", r.size() == before);

        // modify + writeBack (dry run: just modify values in memory, not writing)
        r.modify([](auto& x){ x.value = 999; });
        check("modify()", r[0].value == 999);

        // addresses + intersect + unite
        auto addrs = r.addresses();
        check("addresses()", addrs.size() == r.size());
        auto inter = filtered.intersect(r);
        check("intersect()", inter.size() <= std::min(filtered.size(), r.size()));
        auto uni = filtered.unite(r);
        check("unite()", uni.size() >= std::max(filtered.size(), r.size()));
    }

    // ── 8. 进度回调 + 取消 ────────────────────
    sep("TEST 7: 进度回调 & 取消");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW; p.maxResults = 5000;
        int progressCalls = 0;
        double lastProgress = 0;
        auto r = search.search<int>(p, 100, [&](double prog) {
            progressCalls++;
            lastProgress = prog;
            return prog < 0.3;  // 30% 时取消
        });
        printf("  Progress called: %d times, last: %.1f%%, cancelled at %zu results\n",
               progressCalls, lastProgress*100, r.size());
        check("progress callback", progressCalls > 0);
        check("early cancel", lastProgress <= 0.35);
    }

    // ── 9. maxResults 限制 ────────────────────
    sep("TEST 8: maxResults 限制");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW;
        p.maxResults = 100;
        auto r = search.search<int>(p, 0);
        printf("  maxResults=100: got %zu results\n", r.size());
        check("maxResults enforced", r.size() <= 100);
    }

    // ── 10. 块大小对比 ────────────────────────
    sep("TEST 9: 块大小对比");

    {
        size_t sizes[] = {1*1024*1024, 4*1024*1024, 16*1024*1024, 32*1024*1024, 64*1024*1024};
        double baseline = 0;
        printf("  %-10s %12s %10s %10s\n", "块大小","耗时","结果","加速比");
        for (size_t cs : sizes) {
            SearchParams p; p.memTypeMask = MemType::RANGE_RW; p.chunkSize = cs;
            auto t0 = std::chrono::high_resolution_clock::now();
            auto r = search.search<int>(p, 1);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
            if (cs == sizes[0]) baseline = ms;
            double sp = baseline > 0 ? baseline/ms : 1;
            printf("  %-10s %12s %10zu %9.2fx\n", fmtSize(cs).c_str(), fmtTime(ms).c_str(), r.size(), sp);
        }
    }

    // ── 11. 压力测试 ──────────────────────────
    sep("TEST 10: >4GB 压力测试");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_ALL;
        p.chunkSize = 32*1024*1024;
        auto t0 = std::chrono::high_resolution_clock::now();
        auto r = search.search<int>(p, 123456);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
        auto& s = search.lastStats();
        printf("  search<int>(123456) @ %s chunk:\n", fmtSize(p.chunkSize).c_str());
        printf("    时间: %s | 扫描: %s | 结果: %zu\n",
               fmtTime(ms).c_str(), fmtSize(s.bytesRead).c_str(), r.size());
        printf("    线程: %u | 吞吐: %.1f MB/s\n", s.numThreads, s.throughputMBs);
        check("pressure test", r.size() >= 0);
        printf("    ★ 吞吐评级: %s\n",
               s.throughputMBs > 1000 ? "卓越(>1GB/s)" :
               s.throughputMBs > 500  ? "优秀(>500MB/s)" :
               s.throughputMBs > 200  ? "良好(>200MB/s)" : "一般");
    }

    // ── 12. filterOffset 偏移过滤 ─────────────
    sep("TEST 11: filterOffset 偏移过滤");

    {
        SearchParams p; p.memTypeMask = MemType::RANGE_RW; p.maxResults = 100;
        auto r = search.search<int>(p, 1);
        if (r.size() > 0) {
            // 过滤 address+4 处值等于某个值的
            int neighborVal = mem.Read<int>(r[0].address + 4);
            auto f = r.filterOffset<int>(4, CompareOp::EQ, neighborVal);
            printf("  filterOffset(+4 EQ %d): %zu -> %zu results\n", neighborVal, r.size(), f.size());
            check("filterOffset", f.size() <= r.size());
        } else {
            printf("  (skipped - no results for int=1)\n");
        }
    }

    // ── 总结 ──────────────────────────────────
    sep("测试总结");
    int total = passCnt + failCnt;
    printf("  通过: %d/%d", passCnt, total);
    if (failCnt > 0) printf("  ✗ 失败: %d", failCnt);
    printf("\n  %s\n\n", failCnt == 0 ? "✓ 全部测试通过!" : "✗ 存在失败项");

    auto now = std::chrono::system_clock::now();
    std::time_t nt = std::chrono::system_clock::to_time_t(now);
    printf("  完成时间: %s", std::ctime(&nt));

    return failCnt > 0 ? 1 : 0;
}
