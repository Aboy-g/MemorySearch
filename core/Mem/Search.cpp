#include "Search.hpp"
#include <cstring>
#include <algorithm>
#include <cctype>

// 辅助函数实现

std::vector<uintptr_t> Search::search_bytes(uintptr_t start, uintptr_t end, const void *value, size_t size)
{
    std::vector<uintptr_t> results;
    if (size == 0)
        return results;

    const size_t CHUNK_SIZE = 1024 * 1024;
    std::vector<uint8_t> buffer(CHUNK_SIZE + size - 1);

    uintptr_t cur = start;
    while (cur < end)
    {
        size_t toRead = std::min(CHUNK_SIZE + size - 1, end - cur);
        if (!m_mem.read(cur, buffer.data(), toRead))
        {
            cur += CHUNK_SIZE;
            continue;
        }
        size_t limit = toRead - size + 1;
        for (size_t i = 0; i < limit; ++i)
        {
            if (memcmp(buffer.data() + i, value, size) == 0)
                results.push_back(cur + i);
        }
        cur += CHUNK_SIZE;
    }
    return results;
}

bool Search::parsePatternString(const std::string &patternStr,
                                std::vector<uint8_t> &pattern,
                                std::vector<uint8_t> &mask)
{
    pattern.clear();
    mask.clear();
    std::string hex;
    for (char ch : patternStr)
    {
        if (ch == ' ')
        {
            if (!hex.empty())
            {
                if (hex == "?" || hex == "??")
                {
                    pattern.push_back(0x00);
                    mask.push_back(0x00);
                }
                else
                {
                    if (hex.size() != 2)
                        return false;
                    int val = 0;
                    if (sscanf(hex.c_str(), "%02x", &val) != 1)
                        return false;
                    pattern.push_back(static_cast<uint8_t>(val));
                    mask.push_back(0xFF);
                }
                hex.clear();
            }
        }
        else
        {
            hex.push_back(std::tolower(ch));
        }
    }
    if (!hex.empty())
    {
        if (hex == "?" || hex == "??")
        {
            pattern.push_back(0x00);
            mask.push_back(0x00);
        }
        else
        {
            if (hex.size() != 2)
                return false;
            int val = 0;
            if (sscanf(hex.c_str(), "%02x", &val) != 1)
                return false;
            pattern.push_back(static_cast<uint8_t>(val));
            mask.push_back(0xFF);
        }
    }
    return !pattern.empty();
}

std::vector<uintptr_t> Search::findPatternSingleThread(const SearchParams &params,
                                                       const std::vector<uint8_t> &pattern,
                                                       const std::vector<uint8_t> &mask)
{
    std::vector<uintptr_t> results;
    auto maps = Process::get_process_maps(m_mem.get_pid());
    const size_t CHUNK_SIZE = 1024 * 1024;
    const size_t patternLen = pattern.size();
    std::vector<uint8_t> buffer(CHUNK_SIZE + patternLen - 1);

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
        while (cur < end)
        {
            size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
            if (!m_mem.read(cur, buffer.data(), toRead))
            {
                cur += CHUNK_SIZE;
                continue;
            }
            size_t limit = toRead - patternLen + 1;
            for (size_t i = 0; i < limit; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j)
                {
                    if ((mask[j] != 0) && (buffer[i + j] != pattern[j]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    results.push_back(cur + i);
            }
            cur += CHUNK_SIZE;
        }
    }
    return results;
}

std::vector<uintptr_t> Search::findStringUTF8SingleThread(const SearchParams &params,
                                                          const std::string &str,
                                                          bool includeNull,
                                                          bool caseSensitive)
{
    std::vector<uint8_t> pattern;
    for (char c : str)
        pattern.push_back(caseSensitive ? static_cast<uint8_t>(c) : static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c))));
    if (includeNull)
        pattern.push_back(0);
    std::vector<uint8_t> mask(pattern.size(), 0xFF);
    return findPatternSingleThread(params, pattern, mask);
}

std::vector<uintptr_t> Search::findStringUTF16SingleThread(const SearchParams &params,
                                                           const std::u16string &str,
                                                           bool includeNull,
                                                           bool caseSensitive)
{
    std::vector<uint8_t> pattern;
    for (char16_t ch : str)
    {
        char16_t out = ch;
        if (!caseSensitive && ch < 128)
            out = static_cast<char16_t>(std::tolower(static_cast<int>(ch)));
        pattern.push_back(static_cast<uint8_t>(out & 0xFF));
        pattern.push_back(static_cast<uint8_t>((out >> 8) & 0xFF));
    }
    if (includeNull)
    {
        pattern.push_back(0);
        pattern.push_back(0);
    }
    std::vector<uint8_t> mask(pattern.size(), 0xFF);
    return findPatternSingleThread(params, pattern, mask);
}

std::vector<uintptr_t> Search::findPattern(const SearchParams &params,
                                           const std::vector<uint8_t> &pattern,
                                           const std::vector<uint8_t> &mask)
{
    if (pattern.empty())
        return {};

    // 处理掩码
    std::vector<uint8_t> effectiveMask = mask;
    if (effectiveMask.empty())
        effectiveMask.resize(pattern.size(), 0xFF);
    if (effectiveMask.size() != pattern.size())
        throw std::invalid_argument("Mask size must equal pattern size");

    if (!params.parallel)
        return findPatternSingleThread(params, pattern, effectiveMask);

    // 并行版本
    auto checker = [&](uintptr_t base, const uint8_t *buffer, size_t bufSize, std::vector<uintptr_t> &out)
    {
        const size_t patLen = pattern.size();
        for (size_t i = 0; i + patLen <= bufSize; ++i)
        {
            bool match = true;
            for (size_t j = 0; j < patLen; ++j)
            {
                if (effectiveMask[j] && buffer[i + j] != pattern[j])
                {
                    match = false;
                    break;
                }
            }
            if (match)
                out.push_back(base + i);
        }
    };
    return parallelScan(params, checker);
}

std::vector<uintptr_t> Search::scanPattern(const SearchParams &params,
                                           const std::string &patternStr)
{
    std::vector<uint8_t> pattern, mask;
    if (!parsePatternString(patternStr, pattern, mask))
        return {};
    return findPattern(params, pattern, mask);
}

void Search::scanPatternAsync(const SearchParams &params,
                              const std::string &patternStr,
                              std::function<bool(uintptr_t address)> callback)
{
    std::vector<uint8_t> pattern, mask;
    if (!parsePatternString(patternStr, pattern, mask))
        return;

    auto ranges = getFilteredRanges(params);
    const size_t CHUNK_SIZE = 1024 * 1024;
    const size_t patternLen = pattern.size();
    std::vector<uint8_t> buffer(CHUNK_SIZE + patternLen - 1);

    if (params.parallel)
    {
        // 并行版本
        auto checker = [&](uintptr_t base, const uint8_t *buf, size_t size,
                           std::atomic<bool> &stopFlag,
                           std::function<bool(uintptr_t)> &cb)
        {
            const size_t patLen = pattern.size();
            for (size_t i = 0; i + patLen <= size; ++i)
            {
                if (stopFlag.load())
                    return;
                bool match = true;
                for (size_t j = 0; j < patLen; ++j)
                {
                    if (mask[j] && buf[i + j] != pattern[j])
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                {
                    uintptr_t addr = base + i;
                    if (!cb(addr))
                    {
                        stopFlag.store(true);
                        return;
                    }
                }
            }
        };
        parallelScanAsync(params, checker, callback);
    }
    else
    {
        // 单线程版本

        for (const auto &range : ranges)
        {
            uintptr_t cur = range.start;
            uintptr_t end = range.end;
            while (cur < end)
            {
                size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
                if (!m_mem.read(cur, buffer.data(), toRead))
                {
                    cur += CHUNK_SIZE;
                    continue;
                }
                size_t limit = toRead - patternLen + 1;
                for (size_t i = 0; i < limit; ++i)
                {
                    bool match = true;
                    for (size_t j = 0; j < patternLen; ++j)
                    {
                        if ((mask[j] != 0) && (buffer[i + j] != pattern[j]))
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                    {
                        uintptr_t foundAddr = cur + i;
                        if (!callback(foundAddr))
                            return;
                    }
                }
                cur += CHUNK_SIZE;
            }
        }
    }
}

std::vector<uintptr_t> Search::findStringUTF8(const SearchParams &params,
                                              const std::string &str,
                                              bool includeNull,
                                              bool caseSensitive)
{
    if (str.empty())
        return {};

    if (!params.parallel)
        return findStringUTF8SingleThread(params, str, includeNull, caseSensitive);

    // 并行版本
    std::vector<uint8_t> pattern;
    for (char c : str)
        pattern.push_back(caseSensitive ? static_cast<uint8_t>(c) : static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c))));
    if (includeNull)
        pattern.push_back(0);

    if (caseSensitive)
    {
        std::vector<uint8_t> mask(pattern.size(), 0xFF);
        return findPattern(params, pattern, mask); // 复用并行模式搜索
    }
    else
    {
        // 大小写不敏感需要自定义比较
        auto checker = [&](uintptr_t base, const uint8_t *buffer, size_t bufSize, std::vector<uintptr_t> &out)
        {
            const size_t patLen = pattern.size();
            for (size_t i = 0; i + patLen <= bufSize; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patLen; ++j)
                {
                    unsigned char a = std::tolower(buffer[i + j]);
                    unsigned char b = pattern[j];
                    if (a != b)
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    out.push_back(base + i);
            }
        };
        return parallelScan(params, checker);
    }
}

std::vector<uintptr_t> Search::findStringUTF16(const SearchParams &params,
                                               const std::u16string &str,
                                               bool includeNull,
                                               bool caseSensitive)
{
    if (str.empty())
        return {};

    if (!params.parallel)
        return findStringUTF16SingleThread(params, str, includeNull, caseSensitive);

    // 构建模式（小端）
    std::vector<uint8_t> pattern;
    for (char16_t ch : str)
    {
        char16_t out = ch;
        if (!caseSensitive && ch < 128)
            out = static_cast<char16_t>(std::tolower(static_cast<int>(ch)));
        pattern.push_back(static_cast<uint8_t>(out & 0xFF));
        pattern.push_back(static_cast<uint8_t>((out >> 8) & 0xFF));
    }
    if (includeNull)
    {
        pattern.push_back(0);
        pattern.push_back(0);
    }

    if (caseSensitive)
    {
        std::vector<uint8_t> mask(pattern.size(), 0xFF);
        return findPattern(params, pattern, mask);
    }
    else
    {
        auto checker = [&](uintptr_t base, const uint8_t *buffer, size_t bufSize, std::vector<uintptr_t> &out)
        {
            const size_t patLen = pattern.size();
            for (size_t i = 0; i + patLen <= bufSize; ++i)
            {
                bool match = true;
                for (size_t j = 0; j < patLen; j += 2)
                {
                    uint16_t val = buffer[i + j] | (buffer[i + j + 1] << 8);
                    uint16_t expected = pattern[j] | (pattern[j + 1] << 8);
                    if (!caseSensitive && val < 128 && expected < 128)
                    {
                        val = std::tolower(static_cast<int>(val));
                        expected = std::tolower(static_cast<int>(expected));
                    }
                    if (val != expected)
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    out.push_back(base + i);
            }
        };
        return parallelScan(params, checker);
    }
}