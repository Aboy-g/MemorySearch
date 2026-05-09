#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "Membase.hpp"
#include "Process.hpp"
#include <functional>
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <thread>
#include <atomic>

class Search
{
public:
    struct SearchParams
    {
        uintptr_t startAddress = 0;
        uintptr_t endAddress = UINTPTR_MAX;
        uint32_t memTypeMask = MemType::RANGE_ALL;
        bool align = true;
        bool parallel = true;        // 是否启用多线程
        unsigned int numThreads = 0;  // 线程数（0=自动检测）
    };

    template <typename T>
    struct SearchResult
    {
        uintptr_t address;
        T value;
    };

    // 结果集（嵌套类模板）
    template <typename T>
    class ResultSet
    {
    public:
        using Result = SearchResult<T>;
        using FilterFunc = std::function<bool(const Result &)>;
        using ModifyFunc = std::function<void(Result &)>;

        ResultSet(MemBase &mem, std::vector<Result> results)
            : m_mem(mem), m_results(std::move(results)) {}

        ResultSet(ResultSet &&other) noexcept
            : m_mem(other.m_mem), m_results(std::move(other.m_results)) {}

        ResultSet &operator=(ResultSet &&other) noexcept
        {
            if (this != &other)
            {
                m_results = std::move(other.m_results);
            }
            return *this;
        }

        ResultSet(const ResultSet &) = delete;
        ResultSet &operator=(const ResultSet &) = delete;

        const std::vector<Result> &results() const { return m_results; }
        size_t size() const { return m_results.size(); }
        bool empty() const { return m_results.empty(); }

        ResultSet filter(FilterFunc pred) const
        {
            std::vector<Result> filtered;
            for (const auto &res : m_results)
                if (pred(res))
                    filtered.push_back(res);
            return ResultSet(m_mem, std::move(filtered));
        }

        ResultSet &filterSelf(FilterFunc pred)
        {
            auto it = std::remove_if(m_results.begin(), m_results.end(),
                                     [&](const Result &r) { return !pred(r); });
            m_results.erase(it, m_results.end());
            return *this;
        }

        ResultSet &modify(ModifyFunc func)
        {
            for (auto &res : m_results)
                func(res);
            return *this;
        }

        ResultSet &refresh()
        {
            for (auto &res : m_results)
                m_mem.read(res.address, &res.value, sizeof(T));
            return *this;
        }

        bool writeBack() const
        {
            bool ok = true;
            for (const auto &res : m_results)
                if (!m_mem.Write(res.address, res.value))
                    ok = false;
            return ok;
        }

        bool writeAll(const T &newValue) const
        {
            bool ok = true;
            for (const auto &res : m_results)
                if (!m_mem.Write(res.address, newValue))
                    ok = false;
            return ok;
        }

        bool writeOffset(size_t offset, const T &newValue) const
        {
            bool ok = true;
            for (const auto &res : m_results)
                if (!m_mem.Write(res.address + offset, newValue))
                    ok = false;
            return ok;
        }

        void clear() { m_results.clear(); }

        ResultSet intersect(const ResultSet &other) const
        {
            std::unordered_set<uintptr_t> addrs;
            for (const auto &r : other.m_results)
                addrs.insert(r.address);
            std::vector<Result> inter;
            for (const auto &r : m_results)
                if (addrs.count(r.address))
                    inter.push_back(r);
            return ResultSet(m_mem, std::move(inter));
        }

        ResultSet unite(const ResultSet &other) const
        {
            std::unordered_map<uintptr_t, Result> map;
            for (const auto &r : m_results)
                map[r.address] = r;
            for (const auto &r : other.m_results)
                if (!map.count(r.address))
                    map[r.address] = r;
            std::vector<Result> united;
            united.reserve(map.size());
            for (auto &p : map)
                united.push_back(p.second);
            return ResultSet(m_mem, std::move(united));
        }

        std::vector<uintptr_t> addresses() const
        {
            std::vector<uintptr_t> addrs;
            addrs.reserve(m_results.size());
            for (const auto &r : m_results)
                addrs.push_back(r.address);
            return addrs;
        }

        Result& operator[](size_t offset)
        {
            return m_results[offset];
        }

    private:
        MemBase &m_mem;
        std::vector<Result> m_results;
    };

    explicit Search(MemBase &mem) : m_mem(mem) {}
    Search() = delete;

    // 按值精确搜索（模板）
    template <typename T>
    ResultSet<T> find(const SearchParams &params, T value);

    // 自定义条件搜索
    template <typename T>
    ResultSet<T> find(const SearchParams &params,
                      std::function<bool(const SearchResult<T> &)> judge);

    // 便捷重载：使用默认 SearchParams
    template <typename T>
    ResultSet<T> find(T value)
    {
        SearchParams params;
        return find<T>(params, value);
    }

    // 搜索 UTF-8 字符串
    std::vector<uintptr_t> findStringUTF8(const SearchParams &params,
                                          const std::string &str,
                                          bool includeNull = true,
                                          bool caseSensitive = true);

    // 搜索 UTF-16 字符串（小端序）
    std::vector<uintptr_t> findStringUTF16(const SearchParams &params,
                                           const std::u16string &str,
                                           bool includeNull = true,
                                           bool caseSensitive = true);

    // 搜索原始字节模式（支持通配符掩码）
    std::vector<uintptr_t> findPattern(const SearchParams &params,
                                       const std::vector<uint8_t> &pattern,
                                       const std::vector<uint8_t> &mask = {});

    // 从字符串解析模式（如 "90 90 ?? 90"）
    std::vector<uintptr_t> scanPattern(const SearchParams &params,
                                       const std::string &patternStr);

    // 异步扫描（结果通过回调返回，适合大量结果或持续监控）
    void scanPatternAsync(const SearchParams &params,
                          const std::string &patternStr,
                          std::function<bool(uintptr_t address)> callback);

private:
    MemBase &m_mem;

    struct MemoryRange
    {
        uintptr_t start;
        uintptr_t end;
    };

    // 获取经过过滤和裁剪的内存区域列表
    std::vector<MemoryRange> getFilteredRanges(const SearchParams &params) const;

    // ---------- 并行扫描框架（模板） ----------
    // CheckFunc 签名：void(uintptr_t base, const uint8_t* buffer, size_t bufSize, std::vector<uintptr_t>& out)
    template <typename CheckFunc>
    std::vector<uintptr_t> parallelScan(const SearchParams &params, CheckFunc &&checker) const;

    // ---------- 单线程版本的内部实现
    std::vector<uintptr_t> findPatternSingleThread(const SearchParams &params,
                                                   const std::vector<uint8_t> &pattern,
                                                   const std::vector<uint8_t> &mask);

    std::vector<uintptr_t> findStringUTF8SingleThread(const SearchParams &params,
                                                      const std::string &str,
                                                      bool includeNull,
                                                      bool caseSensitive);

    std::vector<uintptr_t> findStringUTF16SingleThread(const SearchParams &params,
                                                       const std::u16string &str,
                                                       bool includeNull,
                                                       bool caseSensitive);

    // 字节块扫描辅助（用于单线程）
    std::vector<uintptr_t> search_bytes(uintptr_t start, uintptr_t end, const void *value, size_t size);

    bool parsePatternString(const std::string &patternStr,
                            std::vector<uint8_t> &pattern,
                            std::vector<uint8_t> &mask);
};

// 获取过滤后的内存区域
inline std::vector<Search::MemoryRange> Search::getFilteredRanges(const SearchParams &params) const
{
    auto maps = Process::get_process_maps(m_mem.get_pid());
    std::vector<MemoryRange> ranges;
    for (const auto &map : maps)
    {
        if (!map.isValid())
            continue;
        uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
        if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
            continue;
        uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
        uintptr_t end = std::min(static_cast<uintptr_t>(map.endAddress), params.endAddress);
        if (start < end)
            ranges.push_back({start, end});
    }
    return ranges;
}

// 并行扫描框架
template <typename CheckFunc>
inline std::vector<uintptr_t> Search::parallelScan(const SearchParams &params, CheckFunc &&checker) const
{
    auto ranges = getFilteredRanges(params);
    if (ranges.empty())
        return {};

    // 确定线程数
    unsigned int numThreads = params.numThreads;
    if (numThreads == 0)
    {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0)
            numThreads = 4;
    }
    numThreads = std::min(numThreads, static_cast<unsigned int>(ranges.size()));

    // 每个线程独立的结果集
    std::vector<std::vector<uintptr_t>> threadResults(numThreads);
    std::atomic<size_t> nextIdx{0};

    // 工作线程函数
    auto worker = [&](int tid)
    {
        const size_t CHUNK_SIZE = 1024 * 1024; // 1MB
        std::vector<uint8_t> buffer(CHUNK_SIZE);
        while (true)
        {
            size_t idx = nextIdx.fetch_add(1);
            if (idx >= ranges.size())
                break;
            const auto &range = ranges[idx];
            uintptr_t cur = range.start;
            uintptr_t end = range.end;
            while (cur < end)
            {
                size_t toRead = std::min(CHUNK_SIZE, end - cur);
                if (!m_mem.read(cur, buffer.data(), toRead))
                {
                    cur += CHUNK_SIZE;
                    continue;
                }
                // 调用用户提供的检查函数
                checker(cur, buffer.data(), toRead, threadResults[tid]);
                cur += CHUNK_SIZE;
            }
        }
    };

    // 启动线程
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < numThreads; ++i)
        threads.emplace_back(worker, i);
    for (auto &t : threads)
        t.join();

    // 合并结果
    size_t total = 0;
    for (const auto &v : threadResults)
        total += v.size();
    std::vector<uintptr_t> results;
    results.reserve(total);
    for (auto &v : threadResults)
        results.insert(results.end(), v.begin(), v.end());
    return results;
}

// 按值搜索（模板）
template <typename T>
inline Search::ResultSet<T> Search::find(const SearchParams &params, T value)
{
    // 单线程路径
    if (!params.parallel)
    {
        std::vector<SearchResult<T>> results;
        auto maps = Process::get_process_maps(m_mem.get_pid());
        for (const auto &map : maps)
        {
            if (!map.isValid())
                continue;
            uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
            if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
                continue;
            uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
            uintptr_t end = std::min(static_cast<uintptr_t>(map.endAddress), params.endAddress);
            if (start >= end)
                continue;

            auto addrs = search_bytes(start, end, &value, sizeof(T));
            for (uintptr_t addr : addrs)
            {
                if (params.align && (addr % sizeof(T) != 0))
                    continue;
                T actual;
                if (m_mem.read(addr, &actual, sizeof(T)))
                    results.push_back({addr, actual});
            }
        }
        return ResultSet<T>(m_mem, std::move(results));
    }

    // 并行路径：利用 pattern 扫描（将 value 视为字节序列）
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    std::vector<uint8_t> pattern(bytes, bytes + sizeof(T));
    std::vector<uint8_t> mask(pattern.size(), 0xFF);

    // 构造 checker，直接在 buffer 中比较字节，找到地址后读取 T 值
    std::vector<SearchResult<T>> results;
    std::mutex resultMutex; // 保护 results（因为地址分散，也可各线程局部合并后加锁一次）
    auto checker = [&](uintptr_t base, const uint8_t *buffer, size_t bufSize, std::vector<uintptr_t> &localAddrs)
    {
        const size_t step = params.align ? sizeof(T) : 1;
        const size_t patLen = pattern.size();
        for (size_t i = 0; i + patLen <= bufSize; i += step)
        {
            if (params.align && ((base + i) % sizeof(T) != 0))
                continue;
            if (memcmp(buffer + i, pattern.data(), patLen) == 0)
            {
                localAddrs.push_back(base + i);
            }
        }
    };

    auto addrs = parallelScan(params, checker);
    for (uintptr_t addr : addrs)
    {
        T actual;
        if (m_mem.read(addr, &actual, sizeof(T)))
            results.push_back({addr, actual});
    }
    return ResultSet<T>(m_mem, std::move(results));
}

// 自定义条件搜索（模板）
template <typename T>
inline Search::ResultSet<T> Search::find(const SearchParams &params,
                                         std::function<bool(const SearchResult<T> &)> judge)
{
    if (!judge)
        return ResultSet<T>(m_mem, {});

    if (!params.parallel)
    {
        // 单线程版本（原实现）
        std::vector<SearchResult<T>> results;
        auto maps = Process::get_process_maps(m_mem.get_pid());
        const size_t typeSize = sizeof(T);
        const size_t step = params.align ? typeSize : 1;
        const size_t CHUNK_SIZE = 1024 * 1024;
        const size_t OVERLAP = typeSize - 1;
        std::vector<uint8_t> buffer(CHUNK_SIZE + OVERLAP);
        T valueBuf;

        for (const auto &map : maps)
        {
            if (!map.isValid())
                continue;
            uint32_t mapMask = map.getMemType();
            if (params.memTypeMask != MemType::RANGE_ALL && (mapMask & params.memTypeMask) == 0)
                continue;
            uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
            uintptr_t end = std::min(static_cast<uintptr_t>(map.endAddress), params.endAddress);
            if (start >= end)
                continue;

            uintptr_t cur = start;
            if (params.align)
            {
                uintptr_t rem = cur % typeSize;
                if (rem != 0)
                    cur += (typeSize - rem);
            }

            while (cur < end)
            {
                size_t toRead = std::min(CHUNK_SIZE + OVERLAP, end - cur);
                if (!m_mem.read(cur, buffer.data(), toRead))
                {
                    cur += CHUNK_SIZE;
                    continue;
                }
                size_t limit = toRead - typeSize + 1;
                for (size_t offset = 0; offset < limit; offset += step)
                {
                    memcpy(&valueBuf, buffer.data() + offset, typeSize);
                    SearchResult<T> res{cur + offset, valueBuf};
                    if (judge(res))
                        results.push_back(res);
                }
                cur += CHUNK_SIZE;
            }
        }
        return ResultSet<T>(m_mem, std::move(results));
    }

    // 并行版本：每个线程读取 T 并判断
    std::vector<SearchResult<T>> results;
    std::mutex resultMutex;
    const size_t typeSize = sizeof(T);
    const size_t step = params.align ? typeSize : 1;

    auto checker = [&](uintptr_t base, const uint8_t *buffer, size_t bufSize, std::vector<uintptr_t> &localAddrs)
    {
        // 注意：localAddrs 存储地址，后续再读取 T 并通过 judge
        for (size_t offset = 0; offset + typeSize <= bufSize; offset += step)
        {
            if (params.align && ((base + offset) % typeSize != 0))
                continue;
            T val;
            memcpy(&val, buffer + offset, typeSize);
            SearchResult<T> res{base + offset, val};
            if (judge(res))
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                results.push_back(res);
            }
        }
    };
    parallelScan(params, checker);
    return ResultSet<T>(m_mem, std::move(results));
}

#endif // SEARCH_HPP