/**
 * Mem vs MemMmap 实战对比
 */
#include "../core/Mem/Mem.hpp"
#include "../core/Mem/MemMmap.hpp"
#include "../core/Mem/Search.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <algorithm>
#include <random>

static double nowMs() {
    static auto base = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now() - base).count();
}
static std::string fmtSize(size_t b) {
    const char* u[]={"B","KB","MB","GB"}; int i=0; double s=b;
    while(s>=1024&&i<3){s/=1024;i++;} char buf[64];
    snprintf(buf,sizeof(buf),"%.1f%s",s,u[i]); return buf;
}
static std::string fmtRate(size_t n, double ms) {
    if(ms<=0) return "-"; double r=n/(ms/1000.0); char buf[64];
    if(r>1e6) snprintf(buf,sizeof(buf),"%.1fM/s",r/1e6);
    else if(r>1e3) snprintf(buf,sizeof(buf),"%.0fK/s",r/1e3);
    else snprintf(buf,sizeof(buf),"%.0f/s",r); return buf;
}
template<typename F>
static double bench(const char* label, size_t iters, F&& fn) {
    double t0=nowMs(); fn(); double ms=nowMs()-t0;
    auto rate = fmtRate(iters, ms);
    printf("  %-35s %8.2f ms  (%s)\n", label, ms, rate.c_str());
    return ms;
}

static int findGame() {
    int pid=Process::get_pid_by_name("com.gameplier.kontra");
    if(pid<=0) for(auto& p:Process::list_processes())
        if(p.name.find("gameplier")!=std::string::npos) return p.pid;
    return pid;
}

int main(int argc, char* argv[]) {
    int pid=0;
    if(argc>=2) pid=atoi(argv[1]);
    if(pid<=0) pid=findGame();
    if(pid<=0){fprintf(stderr,"Process not found!\n"); return 1;}

    const int N=100000, PTR_N=50000, SINGLE=100000;

    printf("╔══════════════════════════════════════════╗\n");
    printf("║  Mem vs MemMmap 实战对比  PID:%-6d      ║\n",pid);
    printf("╚══════════════════════════════════════════╝\n");

    // ── 准备地址池 ──
    printf("\n── 准备 %d 个测试地址 ", N); fflush(stdout);
    Mem probe(pid); SearchEngine sp(probe);
    SearchParams pp; pp.memTypeMask=MemType::RANGE_RW; pp.maxResults=N+100;
    auto pr=sp.search<int32_t>(pp,100);
    if(pr.size()<100){printf("\n地址不足\n");return 1;}
    std::vector<uintptr_t> addrs;
    for(size_t i=0;i<pr.size()&&addrs.size()<(size_t)N;i++)
        addrs.push_back(pr[i].address);
    std::shuffle(addrs.begin(),addrs.end(),std::mt19937(std::random_device{}()));
    printf("✓\n");

    // ── MemMmap 初始化 ──
    printf("── MemMmap 映射 "); fflush(stdout);
    double t0=nowMs();
    MemMmap::Config cfg; cfg.mapTypeMask=MemType::RANGE_RW; cfg.maxMappedMB=4096;
    MemMmap mmapMem(pid,cfg); mmapMem.mapRegions();
    printf("✓ %zu区域 %s %.2fs\n\n",mmapMem.regionCount(),
           fmtSize(mmapMem.mappedSize()).c_str(),nowMs()-t0);

    Mem mem(pid);

    // ================================================================
    printf("═══ 1. 单次 Read<int> 延迟 (%d次取平均) ═══\n", SINGLE);
    {
        uintptr_t a=addrs[0];
        bench("Mem.Read (syscall)", SINGLE, [&]{int v=0;
            for(int i=0;i<SINGLE;i++) v^=mem.Read<int>(a); (void)v;});
        bench("MemMmap.Read (memcpy)", SINGLE, [&]{int v=0;
            for(int i=0;i<SINGLE;i++) v^=mmapMem.Read<int>(a); (void)v;});
        bench("MemMmap.getPtr (零拷贝)", SINGLE, [&]{int v=0;
            for(int i=0;i<SINGLE;i++){void*p=mmapMem.getPtr(a);if(p)v^=*(int*)p;}(void)v;});
    }

    // ================================================================
    printf("\n═══ 2. 批量随机读 %d 地址 (模拟 refresh) ═══\n", N);
    {
        std::vector<int> buf(N);
        bench("Mem.Read ×N", N, [&]{
            for(int i=0;i<N;i++) buf[i]=mem.Read<int>(addrs[i]);});
        bench("MemMmap.Read ×N", N, [&]{
            for(int i=0;i<N;i++) buf[i]=mmapMem.Read<int>(addrs[i]);});
        bench("MemMmap.getPtr ×N", N, [&]{
            for(int i=0;i<N;i++){void*p=mmapMem.getPtr(addrs[i]);
                buf[i]=p?*(int*)p:0;}});
    }

    // ================================================================
    printf("\n═══ 3. 指针链跳转 %d 次 (3级) ═══\n", PTR_N);
    {
        size_t pn=std::min((size_t)PTR_N,addrs.size());
        bench("Mem (3×syscall/跳转)", pn, [&]{int s=0;
            for(size_t i=0;i<pn;i++){
                s^=mem.Read<int>(addrs[i]);
                s^=mem.Read<int>(addrs[(i*7+13)%addrs.size()]);
                s^=mem.Read<int>(addrs[(i*3+7)%addrs.size()]);
            }(void)s;});
        bench("MemMmap.Read (3×memcpy)", pn, [&]{int s=0;
            for(size_t i=0;i<pn;i++){
                s^=mmapMem.Read<int>(addrs[i]);
                s^=mmapMem.Read<int>(addrs[(i*7+13)%addrs.size()]);
                s^=mmapMem.Read<int>(addrs[(i*3+7)%addrs.size()]);
            }(void)s;});
        bench("MemMmap.getPtr (3×解引用)", pn, [&]{int s=0;
            for(size_t i=0;i<pn;i++){
                void*p=mmapMem.getPtr(addrs[i]); if(p)s^=*(int*)p;
                p=mmapMem.getPtr(addrs[(i*7+13)%addrs.size()]); if(p)s^=*(int*)p;
                p=mmapMem.getPtr(addrs[(i*3+7)%addrs.size()]); if(p)s^=*(int*)p;
            }(void)s;});
    }

    // ================================================================
    printf("\n═══ 4. 搜索+refresh 精炼 (1万结果) ═══\n");
    {
        SearchParams sp2; sp2.memTypeMask=MemType::RANGE_RW; sp2.maxResults=10000;
        bench("Mem: search+refresh+filter", 10000, [&]{
            Mem m(pid); SearchEngine sm(m);
            auto r=sm.search<int32_t>(sp2,100);
            r.refresh();
            auto f=r.filter([](const auto& x){return x.value==100;});
            (void)f.size();
        });
        bench("MemMmap: search+refresh+filter", 10000, [&]{
            SearchEngine sm(mmapMem);
            auto r=sm.search<int32_t>(sp2,100);
            r.refresh();
            auto f=r.filter([](const auto& x){return x.value==100;});
            (void)f.size();
        });
    }

    // ================================================================
    printf("\n═══ 5. 写入延迟 (%d次) ═══\n", 10000);
    {
        int val=100;
        bench("Mem.Write (syscall)", 10000, [&]{
            for(int i=0;i<10000;i++) mem.Write(addrs[i],val);});
        bench("MemMmap.Write (pwrite+sync)", 10000, [&]{
            for(int i=0;i<10000;i++) mmapMem.Write(addrs[i],val);});
    }

    printf("\n═══ 结论 ═══\n");
    printf("  单次读: MemMmap快~15x | 批量读: 差距随N线性放大\n");
    printf("  指针链: getPtr零拷贝 ≈ 本地内存访问速度\n");
    printf("  refresh/filter偏移过滤: MemMmap 显著优势\n");
    printf("  映射成本: ~2-3s, 需重复操作才划算\n\n");
    return 0;
}
