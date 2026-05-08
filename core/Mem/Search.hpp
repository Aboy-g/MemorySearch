#ifndef SEARCH_HPP
#define SEARCH_HPP

#include "Mem.hpp"
#include <functional>
#include <vector>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

class Search
{
public:
    struct SearchParams
    {
        uintptr_t startAddress = 0;
        uintptr_t endAddress = UINTPTR_MAX;
        uint32_t memTypeMask = MemType::RANGE_ALL;
        bool align = true; // 统一小写
    };

    template <typename T>
    struct SearchResult
    {
        uintptr_t address;
        T value;
    };

    // 结果集（嵌套类模板，所有成员函数在类内实现）
    template <typename T>
    class ResultSet
    {
    public:
        using Result = SearchResult<T>;
        using FilterFunc = std::function<bool(const Result &)>;
        using ModifyFunc = std::function<void(Result &)>;

        ResultSet(Mem &mem, std::vector<Result> results)
            : m_mem(mem), m_results(std::move(results)) {}

        // 移动构造和移动赋值
        ResultSet(ResultSet &&other) noexcept
            : m_mem(other.m_mem), m_results(std::move(other.m_results)) {}

        ResultSet &operator=(ResultSet &&other) noexcept
        {
            if (this != &other)
            {
                m_results = std::move(other.m_results);
                //m_mem 引用不需要重新绑定，因为始终指向同一个 Mem 对象
            }
            return *this;
        }

        // 禁止拷贝
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
                                     [&](const Result &r)
                                     { return !pred(r); });
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

    private:
        Mem &m_mem;
        std::vector<Result> m_results;
    };

    explicit Search(Mem &mem) : m_mem(mem) {}
    Search() = delete;

    // 按值精确搜索
    template <typename T>
    ResultSet<T> find(const SearchParams &params, T value)
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
                {
                    results.push_back({addr, actual});
                }
            }
        }
        return ResultSet<T>(m_mem, std::move(results));
    }

    // 自定义条件搜索（高性能分块扫描）
    template <typename T>
    ResultSet<T> find(const SearchParams &params,
                      std::function<bool(const SearchResult<T> &)> judge)
    {
        if (!judge)
            return ResultSet<T>(m_mem, {});

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
            uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
            if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
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
                    {
                        results.push_back(res);
                    }
                }
                cur += CHUNK_SIZE;
            }
        }
        return ResultSet<T>(m_mem, std::move(results));
    }

    // 便捷重载：使用默认 SearchParams
    template <typename T>
    ResultSet<T> find(T value)
    {
        SearchParams params;
        return find<T>(params, value);
    }

       // 搜索 UTF-8 字符串（字节模式匹配）
    std::vector<uintptr_t> findStringUTF8(const SearchParams& params,
                                          const std::string& str,
                                          bool includeNull = true,
                                          bool caseSensitive = true);

    // 搜索 UTF-16 字符串（小端序，每个字符2字节）
    std::vector<uintptr_t> findStringUTF16(const SearchParams& params,
                                           const std::u16string& str,
                                           bool includeNull = true,
                                           bool caseSensitive = true);

    // 搜索原始字节模式（特征码）
    // @param params 搜索参数（地址范围、内存类型、对齐等，注意对齐应设为 false）
    // @param pattern 字节模式，例如 {0x90, 0x90, 0x90, 0x90}
    // @param mask   可选通配符掩码，对应位为 0 表示该字节通配（忽略比较），为 1 表示必须匹配。若为空，则所有字节必须精确匹配
    // @return 匹配的地址列表
    std::vector<uintptr_t> findPattern(const SearchParams& params,
                                       const std::vector<uint8_t>& pattern,
                                       const std::vector<uint8_t>& mask = {});

    std::vector<uintptr_t> scanPattern(const SearchParams& params,
                                       const std::string& patternStr);
    
    void scanPatternAsync(const SearchParams& params,
                          const std::string& patternStr,
                          std::function<bool(uintptr_t address)> callback);

private:
    Mem &m_mem; // 统一命名

    // 辅助搜索函数（按字节模式快速扫描）
    std::vector<uintptr_t> search_bytes(uintptr_t start, uintptr_t end, const void *value, size_t size);

    bool parsePatternString(const std::string &patternStr,
                            std::vector<uint8_t> &pattern,
                            std::vector<uint8_t> &mask);
};

#endif