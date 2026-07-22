/**
 * MemorySearch — 内存模糊搜索工具 v4.0
 * 简化操作, 智能初始化, 一键搜索
 *
 * 用法: fuzzy_tool [PID]
 */

#include "../core/Mem/Mem.hpp"
#include "../core/Mem/MemMmap.hpp"
#include "../core/Mem/Search.hpp"
#include "../core/Mem/FuzzySearch.hpp"
#include <iostream>
#include <sstream>
#include <cmath>
#include <fstream>
#include <atomic>
#include <thread>

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"
#define CLR_GRAY    "\033[90m"

// ==================== 工具 ====================
static std::string fmtSize(size_t b) {
    const char* u[] = {"B","KB","MB","GB"};
    int i = 0; double s = b;
    while (s >= 1024 && i < 3) { s/=1024; i++; }
    char buf[64]; snprintf(buf, sizeof(buf), "%.1f %s", s, u[i]); return buf;
}
static std::string fmtNum(size_t n) {
    if (n < 10000) return std::to_string(n);
    if (n < 1000000) { char b[32]; snprintf(b,32,"%.1fK",n/1000.0); return b; }
    char b[32]; snprintf(b,32,"%.1fM",n/1000000.0); return b;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\n\r")-a+1);
}
static std::string toLower(const std::string& s) {
    std::string r = s; for (auto& c : r) c = std::tolower((unsigned char)c); return r;
}
template<typename T> static bool parseNum(const std::string& s, T& out) {
    std::string t = trim(s); if (t.empty()) return false;
    try {
        size_t p;
        if (t.size()>=2 && t[0]=='0' && (t[1]=='x'||t[1]=='X')) {
            auto v = std::stoull(t.substr(2), &p, 16);
            if (p!=t.size()-2) return false; out = static_cast<T>(v);
        } else if (t.find('.')!=std::string::npos) {
            auto v = std::stod(t, &p);
            if (p!=t.size()) return false; out = static_cast<T>(v);
        } else { auto v = std::stoll(t, &p, 10); if (p!=t.size()) return false; out = static_cast<T>(v); }
        return true;
    } catch(...) { return false; }
}
static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> r; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, d)) if (!t.empty()) r.push_back(t); return r;
}
static bool showProgress(double p) {
    const int W=30; int f=(int)(p*W);
    printf("\r  ["); for(int i=0;i<W;i++) printf("%s",i<f?"█":"░");
    printf("] %4.0f%%",p*100); fflush(stdout); return true;
}
// ==================== 区域定义 ====================
struct RegionInfo { const char* name; uint32_t mask; };
static const RegionInfo REGIONS[] = {
    {"ALL (全内存)",     MemType::RANGE_ALL},
    {"All RW (可读写)",  MemType::RANGE_RW},
    {"JAVA_HEAP (游戏)", MemType::RANGE_JAVA_HEAP},
    {"ANONYMOUS",        MemType::RANGE_ANONYMOUS},
    {"C_ALLOC",          MemType::RANGE_C_ALLOC},
    {"CODE_APP (代码)",  MemType::RANGE_CODE_APP},
};
static const int NUM_REGIONS = sizeof(REGIONS)/sizeof(REGIONS[0]);

// ==================== 类型 ====================
enum class DType { INT32, INT64, FLOAT, DOUBLE };
static const char* tname(DType t) { switch(t){case DType::INT32:return "i32";case DType::INT64:return "i64";case DType::FLOAT:return "f32";case DType::DOUBLE:return"f64";} return"?"; }
static size_t tsize(DType t) { switch(t){case DType::INT32:case DType::FLOAT:return 4;case DType::INT64:case DType::DOUBLE:return 8;} return 4; }

// ==================== 主类 ====================
class FuzzyTool {
public:
    FuzzyTool(MemBase& m) : mem(m), fuzzy(m), search(m) {
        params.chunkSize = 32*1024*1024;
        params.memTypeMask = MemType::RANGE_ALL;
    }

    void init(int pid) {
        // 分析内存, 推荐区域
        auto maps = Process::get_process_maps(pid);
        size_t sizes[NUM_REGIONS] = {};
        for (const auto& m : maps) {
            if (!m.isValid()||!m.readable) continue;
            for (int i=0; i<NUM_REGIONS; i++) {
                uint32_t t = static_cast<uint32_t>(m.getMemType());
                if (REGIONS[i].mask==0 || (t & REGIONS[i].mask))
                    sizes[i] += m.length;
            }
        }

        // 显示选项
        printf(CLR_CYAN "\n  选择搜索区域:\n\n" CLR_RESET);
        for (int i=0; i<NUM_REGIONS; i++) {
            const char* hint = "";
            size_t est = sizes[i]/4;
            if (i==2 && est > 100000000) hint = CLR_YELLOW " ★ 推荐 (游戏数据)" CLR_RESET;
            else if (i==3 && est < 500000000 && est > 1000000) hint = CLR_GRAY " (快速)" CLR_RESET;
            printf("  " CLR_BOLD "%d" CLR_RESET ". %-22s %8s", i+1, REGIONS[i].name, fmtSize(sizes[i]).c_str());
            if (est > 0)
                printf("  (~%s 条 int32)", fmtSize(est).c_str());
            printf("%s\n", hint);
        }

        // 默认选 JAVA_HEAP (REGIONS[2] = 显示编号 3)
        printf(CLR_GRAY "\n  直接回车 = JAVA_HEAP (推荐)" CLR_RESET "\n  > ");
        std::string line;
        std::getline(std::cin, line);
        int choice = 3; // 默认 JAVA_HEAP (显示编号3, REGIONS索引2)
        if (!trim(line).empty()) parseNum(line, choice);
        if (choice < 1 || choice > NUM_REGIONS) choice = 3;

        regionIdx = choice - 1;
        params.memTypeMask = REGIONS[regionIdx].mask;
        printf(CLR_GREEN "  ✓ %s  — %s\n" CLR_RESET,
               REGIONS[regionIdx].name, fmtSize(sizes[regionIdx]).c_str());
    }

    void run() {
        printf(CLR_BOLD CLR_CYAN "\n  ▸ 模糊搜索 v4.0" CLR_RESET "\n");
        printf(CLR_GRAY "  h=帮助  s=搜索  u=未知值  f=精炼  l=列出  m=修改  w=写回  q=退出\n\n" CLR_RESET);

        while (true) {
            // 简洁状态栏
            size_t n = size();
            printf(CLR_GRAY "[" CLR_RESET "%s" CLR_GRAY "|" CLR_RESET "%s" CLR_GRAY "] " CLR_RESET,
                   REGIONS[regionIdx].name,
                   fuzzy.phase()==FuzzySearch::Phase::REGION_SNAPSHOT?"快照":
                   fuzzy.phase()==FuzzySearch::Phase::INDIVIDUAL?"个体":"空闲");
            if (n > 0) printf(CLR_BOLD "%s" CLR_RESET, fmtNum(n).c_str());
            else printf("0");
            printf(" " CLR_GRAY ">" CLR_RESET " ");

            std::string line;
            std::getline(std::cin, line);
            if (!std::cin) break;
            auto parts = split(line, ' ');
            if (parts.empty()) continue;
            std::string cmd = toLower(parts[0]);

            if (cmd=="q") break;
            else if (cmd=="h") help();
            else if (cmd=="r") cmdRegion(parts);
            else if (cmd=="t") cmdType(parts);
            else if (cmd=="s") cmdSearch(parts);
            else if (cmd=="u") cmdUnknown(parts);
            else if (cmd=="f") cmdRefine(parts);
            else if (cmd=="l") cmdList(parts);
            else if (cmd=="m") cmdModify(parts);
            else if (cmd=="ma") cmdModifyAll(parts);
            else if (cmd=="w") cmdWrite();
            else if (cmd=="lock") cmdLock(parts);
            else if (cmd=="e") cmdExport(parts);
            else if (cmd=="v") cmdVerify(parts);
            else if (cmd=="c") cmdClear();
            else if (cmd=="mem") cmdMem();
            else printf(CLR_RED "  ? h=帮助\n" CLR_RESET);
        }
    }

private:
    MemBase& mem;
    FuzzySearch fuzzy;
    SearchEngine search;
    SearchParams params;
    int regionIdx = 2;      // 默认 JAVA_HEAP (REGIONS[2])
    DType dtype = DType::INT32;
    bool lockActive = false;
    std::vector<uintptr_t> lockAddrs;
    std::vector<uint8_t> lockValues;

    size_t size() const { return fuzzy.size(); }

    void help() {
        printf(CLR_BOLD "\n  ═══ 命令 ═══\n" CLR_RESET);
        printf("  " CLR_CYAN "r [N]" CLR_RESET "        切换搜索区域 (r=列表)\n");
        printf("  " CLR_CYAN "t i32|f" CLR_RESET "       数据类型 (默认i32)\n");
        printf("  " CLR_CYAN "s <value>" CLR_RESET "     精确值搜索\n");
        printf("  " CLR_CYAN "u [max]" CLR_RESET "       未知值搜索 → 自动快照\n");
        printf("  " CLR_CYAN "f +|-|~|=|<val>" CLR_RESET " 精炼: 变大/变小/变化/未变/精确值\n");
        printf("  " CLR_CYAN "f >N / f <N" CLR_RESET "    直接过滤: 大于/小于某值\n");
        printf("  " CLR_CYAN "l [N]" CLR_RESET "        列出结果\n");
        printf("  " CLR_CYAN "m <i> <v>" CLR_RESET "     修改指定结果\n");
        printf("  " CLR_CYAN "ma <v>" CLR_RESET "       修改全部\n");
        printf("  " CLR_CYAN "w" CLR_RESET "             写回进程\n");
        printf("  " CLR_CYAN "lock <v>" CLR_RESET "     后台锁定值\n");
        printf("  " CLR_CYAN "e [file]" CLR_RESET "     导出地址列表\n");
        printf("  " CLR_CYAN "v [N]" CLR_RESET "        验证地址: 重读 + 对比快照\n");
        printf("  " CLR_CYAN "c" CLR_RESET "             清空结果\n");
        printf("  " CLR_CYAN "mem" CLR_RESET "           显示内存用量\n");
        printf("  " CLR_CYAN "q" CLR_RESET "             退出\n");
        printf("\n  " CLR_YELLOW "快照精炼: s 100 → (游戏中改值) → f+ 变大/f- 变小/f~ 变化/f= 未变" CLR_RESET "\n");
        printf("  " CLR_GRAY  "直接过滤: s 100 → f >200 或 f <50 或 f 999 (不依赖快照)" CLR_RESET "\n\n");
    }

    void cmdRegion(const std::vector<std::string>& parts) {
        if (parts.size()<2) {
            auto maps = Process::get_process_maps(mem.get_pid());
            for (int i=0;i<NUM_REGIONS;i++) {
                size_t sz=0;
                for (const auto& m:maps)
                    if (m.isValid()&&m.readable) {
                        uint32_t t=static_cast<uint32_t>(m.getMemType());
                        if (REGIONS[i].mask==0||(t&REGIONS[i].mask)) sz+=m.length;
                    }
                printf("  %d. %-22s %8s\n", i+1, REGIONS[i].name, fmtSize(sz).c_str());
            }
            return;
        }
        int idx; if (!parseNum(parts[1],idx)||idx<1||idx>NUM_REGIONS){printf(CLR_RED"  无效\n" CLR_RESET);return;}
        regionIdx=idx-1; params.memTypeMask=REGIONS[regionIdx].mask; fuzzy.reset();
        printf(CLR_GREEN "  ✓ %s\n" CLR_RESET, REGIONS[regionIdx].name);
    }

    void cmdType(const std::vector<std::string>& parts) {
        if (parts.size()<2) { printf("  i32 | i64 | f | d\n"); return; }
        auto t=toLower(parts[1]); fuzzy.reset();
        if (t=="i32"||t=="int") dtype=DType::INT32;
        else if (t=="i64"||t=="long") dtype=DType::INT64;
        else if (t=="f"||t=="float") dtype=DType::FLOAT;
        else if (t=="d"||t=="double") dtype=DType::DOUBLE;
        else { printf(CLR_RED "  i32|i64|f|d\n" CLR_RESET); return; }
        printf(CLR_GREEN "  ✓ %s\n" CLR_RESET, tname(dtype));
    }

    void cmdSearch(const std::vector<std::string>& parts) {
        if (parts.size()<2) { printf("  s <value>\n"); return; }
        fuzzy.reset();
        size_t n = 0;

        switch(dtype) {
        case DType::INT32: { int32_t v; if(!parseNum(parts[1],v)){printf(CLR_RED"  无效\n" CLR_RESET);return;}
            fuzzy.searchValue<int32_t>(params, v, showProgress); printf("\r"); n=fuzzy.size(); break; }
        case DType::INT64: { int64_t v; if(!parseNum(parts[1],v)){printf(CLR_RED"  无效\n" CLR_RESET);return;}
            fuzzy.searchValue<int64_t>(params, v, showProgress); printf("\r"); n=fuzzy.size(); break; }
        case DType::FLOAT: { float v; if(!parseNum(parts[1],v)){printf(CLR_RED"  无效\n" CLR_RESET);return;}
            fuzzy.searchValue<float>(params, v, showProgress); printf("\r"); n=fuzzy.size(); break; }
        case DType::DOUBLE: { double v; if(!parseNum(parts[1],v)){printf(CLR_RED"  无效\n" CLR_RESET);return;}
            fuzzy.searchValue<double>(params, v, showProgress); printf("\r"); n=fuzzy.size(); break; }
        }
        printf(CLR_GREEN "  ✓ %s 条 | 自动快照已创建\n" CLR_RESET, fmtNum(n).c_str());
        if (n>0 && n<=10) listTop(10);
        else if (n>10) printf("  " CLR_GRAY "(改值后 f+/- 精炼)" CLR_RESET "\n");
    }

    void cmdUnknown(const std::vector<std::string>& parts) {
        size_t maxR = 0; if (parts.size()>=2) parseNum(parts[1], maxR);

        if (maxR == 0) {
            // 自动判断: 如果预估 > 阈值, 提示用区域快照
            auto maps = Process::get_process_maps(mem.get_pid());
            size_t est = 0;
            for (const auto& m : maps) {
                if (!m.isValid()||!m.readable) continue;
                uint32_t t=static_cast<uint32_t>(m.getMemType());
                if (params.memTypeMask!=0 && (t&params.memTypeMask)==0) continue;
                est += m.length / tsize(dtype);
            }
            if (est > 200000000) {
                printf(CLR_YELLOW "  预估 %s 条 (>2亿), 将用区域快照模式 (%s 内存)\n" CLR_RESET,
                       fmtNum(est).c_str(), fmtSize(est*tsize(dtype)).c_str());
                printf(CLR_GRAY "  或指定上限: u 5000000\n" CLR_RESET);
            }
        }

        fuzzy.reset();
        if (maxR > 0) fuzzy.m_cfg.maxIndividual = maxR;
        fuzzy.m_cfg.verbose = true;  // 开启诊断输出

        switch(dtype) {
        case DType::INT32: fuzzy.searchUnknown<int32_t>(params, maxR, showProgress); break;
        case DType::INT64: fuzzy.searchUnknown<int64_t>(params, maxR, showProgress); break;
        case DType::FLOAT: fuzzy.searchUnknown<float>(params, maxR, showProgress); break;
        case DType::DOUBLE: fuzzy.searchUnknown<double>(params, maxR, showProgress); break;
        }
        printf("\r");

        if (fuzzy.phase() == FuzzySearch::Phase::REGION_SNAPSHOT) {
            printf(CLR_GREEN "  ✓ 区域快照 %s" CLR_RESET " | "
                   CLR_GRAY "f+/-/~ 精炼时自动重扫\n" CLR_RESET,
                   fmtSize(fuzzy.memoryUsed()).c_str());
        } else {
            printf(CLR_GREEN "  ✓ %s 条" CLR_RESET " | "
                   CLR_GRAY "内存 %s | f+/-/~ 精炼\n" CLR_RESET,
                   fmtNum(size()).c_str(), fmtSize(fuzzy.memoryUsed()).c_str());
        }
    }

    void cmdRefine(const std::vector<std::string>& parts) {
        if (size()==0 && fuzzy.phase()!=FuzzySearch::Phase::REGION_SNAPSHOT) {
            printf(CLR_RED "  先执行 s 或 u\n" CLR_RESET); return;
        }
        if (parts.size()<2) { printf("  f + | - | ~ | = | <value> | >N\n"); return; }

        auto op = parts[1];
        size_t before = size();
        if (fuzzy.phase()==FuzzySearch::Phase::REGION_SNAPSHOT && before==0)
            before = fuzzy.stats().phase1Results;

        if (op=="+"||op=="-"||op=="~"||op=="=") {
            CompareOp cop = op=="+"?CompareOp::INCREASED:op=="-"?CompareOp::DECREASED:
                            op=="~"?CompareOp::CHANGED:CompareOp::UNCHANGED;
            size_t after = 0;
            switch(dtype){case DType::INT32:after=fuzzy.refine<int32_t>(cop);break;
                          case DType::INT64:after=fuzzy.refine<int64_t>(cop);break;
                          case DType::FLOAT:after=fuzzy.refine<float>(cop);break;
                          case DType::DOUBLE:after=fuzzy.refine<double>(cop);break;}
            // 显示: 区域快照模式用估算值, 个体模式用实际值
            printf(CLR_GREEN "  ✓ %s → %s 条\n" CLR_RESET,
                   fmtNum(before).c_str(), fmtNum(after).c_str());
            if (after <= 20 && after > 0) listTop(after);
            else if (after > 100000)
                printf(CLR_YELLOW "  ⚠ 结果较多, 可继续 f+/- 精炼或 s <value> 精确过滤\n" CLR_RESET);
        } else if (op.size() >= 2 && (op[0] == '>' || op[0] == '<')) {
            // 直接比较过滤: f >N 或 f <N (不依赖快照)
            bool gt = (op[0] == '>');
            bool eq = (op.size() > 1 && op[1] == '=');
            CompareOp cop = gt ? (eq ? CompareOp::GTE : CompareOp::GT)
                               : (eq ? CompareOp::LTE : CompareOp::LT);
            size_t after = 0;
            std::string valStr = op.substr(gt && eq ? 2 : 1);
            switch(dtype){case DType::INT32:{int32_t v;if(parseNum(valStr,v))after=fuzzy.filterCompare<int32_t>(cop,v);break;}
                          case DType::INT64:{int64_t v;if(parseNum(valStr,v))after=fuzzy.filterCompare<int64_t>(cop,v);break;}
                          case DType::FLOAT:{float v;if(parseNum(valStr,v))after=fuzzy.filterCompare<float>(cop,v);break;}
                          case DType::DOUBLE:{double v;if(parseNum(valStr,v))after=fuzzy.filterCompare<double>(cop,v);break;}}
            printf(CLR_GREEN "  ✓ %s → %s 条\n" CLR_RESET,
                   fmtNum(before).c_str(), fmtNum(after).c_str());
            if (after <= 20 && after > 0) listTop(after);
        } else {
            // 精确值过滤
            size_t after = 0;
            switch(dtype){case DType::INT32:{int32_t v;if(parseNum(op,v))after=fuzzy.filterExact<int32_t>(v);break;}
                          case DType::INT64:{int64_t v;if(parseNum(op,v))after=fuzzy.filterExact<int64_t>(v);break;}
                          case DType::FLOAT:{float v;if(parseNum(op,v))after=fuzzy.filterExact<float>(v);break;}
                          case DType::DOUBLE:{double v;if(parseNum(op,v))after=fuzzy.filterExact<double>(v);break;}}
            printf(CLR_GREEN "  ✓ %s → %s 条\n" CLR_RESET,
                   fmtNum(before).c_str(), fmtNum(after).c_str());
            if (after <= 20 && after > 0) listTop(after);
        }
    }

    void cmdList(const std::vector<std::string>& parts) {
        if (size()==0) { printf("  (无结果)\n"); return; }
        int n=10; if (parts.size()>=2) parseNum(parts[1],n);
        listTop(n);
    }

    void listTop(size_t n) {
        size_t show=std::min(n,size());
        for (size_t i=0;i<show;i++) {
            printf("  [%zu] 0x%016llx = ", i+1, (unsigned long long)addrAt(i));
            switch(dtype){case DType::INT32:printf("%d\n",fuzzy.valueAt<int32_t>(i));break;
                          case DType::INT64:printf("%lld\n",(long long)fuzzy.valueAt<int64_t>(i));break;
                          case DType::FLOAT:printf("%.6f\n",fuzzy.valueAt<float>(i));break;
                          case DType::DOUBLE:printf("%.12f\n",fuzzy.valueAt<double>(i));break;}
        }
        if (size()>show) printf("  " CLR_GRAY "...(%s more)" CLR_RESET "\n", fmtNum(size()-show).c_str());
    }

    uintptr_t addrAt(size_t i) {
        switch(dtype){case DType::INT32:return fuzzy.addrAt<int32_t>(i);
                      case DType::INT64:return fuzzy.addrAt<int64_t>(i);
                      case DType::FLOAT:return fuzzy.addrAt<float>(i);
                      case DType::DOUBLE:return fuzzy.addrAt<double>(i);}
        return 0;
    }

    void cmdModify(const std::vector<std::string>& parts) {
        if (size()==0) { printf(CLR_RED "  无结果\n" CLR_RESET); return; }
        if (parts.size()<3) { printf("  m <index> <value>\n"); return; }
        int idx; if (!parseNum(parts[1],idx)||idx<1||(size_t)idx>size()){printf(CLR_RED"  无效索引\n" CLR_RESET);return;}
        setOne(idx-1,parts[2]);
        printf(CLR_GREEN "  ✓ [%d] 已修改 (w 写回)\n" CLR_RESET, idx);
    }

    void cmdModifyAll(const std::vector<std::string>& parts) {
        if (size()==0) { printf(CLR_RED "  无结果\n" CLR_RESET); return; }
        if (parts.size()<2) { printf("  ma <value>\n"); return; }
        for (size_t i=0;i<size();i++) setOne(i,parts[1]);
        printf(CLR_GREEN "  ✓ %s 条已修改 (w 写回)\n" CLR_RESET, fmtNum(size()).c_str());
    }

    bool setOne(size_t i, const std::string& s) {
        switch(dtype){case DType::INT32:{int32_t v;if(!parseNum(s,v))return false;fuzzy.setValueAt<int32_t>(i,v);return true;}
                      case DType::INT64:{int64_t v;if(!parseNum(s,v))return false;fuzzy.setValueAt<int64_t>(i,v);return true;}
                      case DType::FLOAT:{float v;if(!parseNum(s,v))return false;fuzzy.setValueAt<float>(i,v);return true;}
                      case DType::DOUBLE:{double v;if(!parseNum(s,v))return false;fuzzy.setValueAt<double>(i,v);return true;}}
        return false;
    }

    void cmdWrite() {
        if (size()==0) { printf(CLR_RED "  无结果\n" CLR_RESET); return; }
        bool ok=false;
        switch(dtype){case DType::INT32:ok=fuzzy.writeBack<int32_t>();break;
                      case DType::INT64:ok=fuzzy.writeBack<int64_t>();break;
                      case DType::FLOAT:ok=fuzzy.writeBack<float>();break;
                      case DType::DOUBLE:ok=fuzzy.writeBack<double>();break;}
        printf(ok?CLR_GREEN "  ✓ 已写回\n" CLR_RESET:CLR_YELLOW "  ⚠ 部分失败\n" CLR_RESET);
    }

    void cmdLock(const std::vector<std::string>& parts) {
        if (lockActive) { lockActive=false; printf("  锁定已停止\n"); return; }
        if (size()==0) { printf(CLR_RED "  无结果\n" CLR_RESET); return; }
        if (parts.size()<2) { printf("  lock <value>\n"); return; }
        lockAddrs.clear(); size_t sz=tsize(dtype);
        for (size_t i=0;i<size();i++) { lockAddrs.push_back(addrAt(i)); setOne(i,parts[1]); }
        lockValues.resize(lockAddrs.size()*sz);
        for (size_t i=0;i<lockAddrs.size();i++) {
            switch(dtype){case DType::INT32:memcpy(&lockValues[i*sz],fuzzy.results<int32_t>().valuePtr(i),sz);break;
                          case DType::INT64:memcpy(&lockValues[i*sz],fuzzy.results<int64_t>().valuePtr(i),sz);break;
                          case DType::FLOAT:memcpy(&lockValues[i*sz],fuzzy.results<float>().valuePtr(i),sz);break;
                          case DType::DOUBLE:memcpy(&lockValues[i*sz],fuzzy.results<double>().valuePtr(i),sz);break;}
        }
        lockActive=true;
        printf(CLR_GREEN "  ✓ 锁定 %s 地址 (500ms)\n" CLR_RESET, fmtNum(lockAddrs.size()).c_str());
        std::thread([this,sz](){
            while(lockActive){ for(size_t i=0;i<lockAddrs.size()&&lockActive;i++)
                mem.write(lockAddrs[i],&lockValues[i*sz],sz);
                std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
        }).detach();
    }

    void cmdExport(const std::vector<std::string>& parts) {
        if (size()==0) { printf("  (无结果)\n"); return; }
        std::string fn="addrs.txt"; if (parts.size()>=2) fn=parts[1];
        std::ofstream f(fn); if(!f){printf(CLR_RED"  无法创建\n" CLR_RESET);return;}
        for (size_t i=0;i<size();i++) f<<"0x"<<std::hex<<addrAt(i)<<std::dec<<"\n";
        printf(CLR_GREEN "  ✓ 已导出 %s 地址 → %s\n" CLR_RESET, fmtNum(size()).c_str(), fn.c_str());
    }

    void cmdClear() { fuzzy.reset(); printf("  ✓ 已清空\n"); }
    void cmdMem() { printf("  内存: %s\n", fmtSize(fuzzy.memoryUsed()).c_str()); }

    void cmdVerify(const std::vector<std::string>& parts) {
        if (size() == 0) { printf("  无结果\n"); return; }
        int n = 5;
        if (parts.size() >= 2) parseNum(parts[1], n);
        n = std::min(n, (int)size());

        printf("  重读前 %d 个地址 (当前值 vs 快照):\n", n);
        for (int i = 0; i < n; i++) {
            uintptr_t addr = addrAt(i);
            // 读当前值
            size_t vsz = tsize(dtype);
            uint8_t curBuf[8] = {};
            bool ok = mem.read(addr, curBuf, vsz);

            // 从快照取旧值
            printf("  [%d] 0x%llx  cur=", i+1, (unsigned long long)addr);
            if (ok) printRaw(curBuf, vsz);
            else printf("?");
            printf("  snap=");
            printSnapVal(i);
            printf("\n");
        }
    }

    void printRaw(const uint8_t* buf, size_t vsz) {
        switch(dtype) {
            case DType::INT32: { int32_t v; memcpy(&v, buf, vsz); printf("%d", v); break; }
            case DType::INT64: { int64_t v; memcpy(&v, buf, vsz); printf("%lld", (long long)v); break; }
            case DType::FLOAT: { float v; memcpy(&v, buf, vsz); printf("%.6f", v); break; }
            case DType::DOUBLE:{ double v; memcpy(&v, buf, vsz); printf("%.12f", v); break; }
        }
    }

    void printSnapVal(size_t i) {
        switch(dtype) {
            case DType::INT32: printf("%d", fuzzy.valueAt<int32_t>(i)); break;
            case DType::INT64: printf("%lld", (long long)fuzzy.valueAt<int64_t>(i)); break;
            case DType::FLOAT: printf("%.6f", fuzzy.valueAt<float>(i)); break;
            case DType::DOUBLE:printf("%.12f", fuzzy.valueAt<double>(i)); break;
        }
    }
};

// ==================== 进程选择 ====================
static int selectProcess() {
    auto procs = Process::list_processes();
    if (procs.empty()) return -1;

    std::vector<Process::ProcessInfo> all;
    for (auto& p : procs) {
        if (p.name.empty()) continue;
        all.push_back(p);
    }
    std::sort(all.begin(), all.end(),
              [](auto& a, auto& b) { return a.name < b.name; });

    printf(CLR_CYAN "\n  输入 编号/PID/包名:\n\n" CLR_RESET);
    printf(CLR_GRAY "  %-6s %-8s %s\n" CLR_RESET, "编号", "PID", "进程名");

    int count = 0;
    for (auto& p : all) {
        printf("  " CLR_BOLD "%3d" CLR_RESET ".  %-8d %s\n", ++count, p.pid, p.name.c_str());
        if (count >= 40) { printf(CLR_GRAY "  ... (%zu more)\n" CLR_RESET, all.size()-40); break; }
    }

    if (count == 0) { printf(CLR_RED "  无运行进程\n" CLR_RESET); return -1; }

    printf(CLR_GRAY "  输入: 编号 | PID | 包名 (q=退出)\n  > " CLR_RESET);
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    if (line.empty() || line == "q" || line == "Q") return -1;

    // 编号
    int idx;
    if (parseNum(line, idx) && idx >= 1 && idx <= count)
        return all[idx-1].pid;

    // PID
    int pid = atoi(line.c_str());
    if (pid > 0) return pid;

    // 包名 — 搜索全部进程 (不限于显示的前40条)
    pid = Process::get_pid_by_name(line.c_str());
    if (pid > 0) return pid;
    for (auto& p : procs)  // 用原始全量列表
        if (p.name.find(line) != std::string::npos) return p.pid;

    printf(CLR_RED "  未找到: %s\n" CLR_RESET, line.c_str());
    return -1;
}

// ==================== 主函数 ====================
int main() {
    printf(CLR_BOLD "\n╔══════════════════════════════╗\n" CLR_RESET);
    printf(CLR_BOLD "║  " CLR_CYAN "MemorySearch 模糊搜索 v4" CLR_RESET CLR_BOLD "  ║\n" CLR_RESET);
    printf(CLR_BOLD "╚══════════════════════════════╝\n" CLR_RESET);

    int pid = selectProcess();
    if (pid <= 0) { printf(CLR_RED "\n  已退出\n" CLR_RESET); return 1; }

    auto name = [&]{
        for (auto& p : Process::list_processes())
            if (p.pid == pid) return p.name;
        return std::string("unknown");
    }();
    printf(CLR_GREEN "  PID: %d" CLR_RESET " | %s | CPU: %u核\n",
           pid, name.c_str(), std::thread::hardware_concurrency());

    // 使用 MemMmap 实现零拷贝
    MemMmap::Config cfg;
    cfg.mapTypeMask = MemType::RANGE_ALL;
    cfg.maxMappedMB = 4096;

    MemMmap mmapMem(pid, cfg);
    printf("  映射内存..."); fflush(stdout);
    mmapMem.mapRegions();
    printf(" ✓ %s\n", fmtSize(mmapMem.mappedSize()).c_str());

    FuzzyTool tool(mmapMem);
    tool.init(pid);
    tool.run();
    return 0;
}
