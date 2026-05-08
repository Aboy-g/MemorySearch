#include "Search.hpp"
#include <cstring>
#include <algorithm>

std::vector<uintptr_t> Search::search_bytes(uintptr_t start, uintptr_t end, const void* value, size_t size) {
    std::vector<uintptr_t> results;
    if (size == 0) return results;

    const size_t CHUNK_SIZE = 1024 * 1024; // 1MB
    std::vector<uint8_t> buffer(CHUNK_SIZE + size - 1);

    uintptr_t cur = start;
    while (cur < end) {
        size_t toRead = std::min(CHUNK_SIZE + size - 1, end - cur);
        if (!m_mem.read(cur, buffer.data(), toRead)) {
            cur += CHUNK_SIZE;
            continue;
        }
        size_t limit = toRead - size + 1;
        for (size_t i = 0; i < limit; ++i) {
            if (memcmp(buffer.data() + i, value, size) == 0) {
                results.push_back(cur + i);
            }
        }
        cur += CHUNK_SIZE;
    }
    return results;
}


std::vector<uintptr_t> Search::findStringUTF8(const Search::SearchParams& params,
                                              const std::string& str,
                                              bool includeNull,
                                              bool caseSensitive) {
    std::vector<uintptr_t> results;
    if (str.empty()) return results;

    // 构建 pattern（字节序列）
    std::vector<char> pattern;
    pattern.reserve(str.size() + (includeNull ? 1 : 0));
    for (char c : str) {
        pattern.push_back(caseSensitive ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (includeNull) pattern.push_back('\0');

    auto maps = Process::get_process_maps(m_mem.get_pid());
    for (const auto& map : maps) {
        if (!map.isValid()) continue;
        uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
        if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
            continue;
        uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
        uintptr_t end   = std::min(static_cast<uintptr_t>(map.endAddress),   params.endAddress);
        if (start >= end) continue;

        if (caseSensitive) {
            // 直接使用字节搜索（前提是 Mem 提供了 search_bytes 公共方法）
            auto addrs = search_bytes(start, end, pattern.data(), pattern.size());
            results.insert(results.end(), addrs.begin(), addrs.end());
        } else {
            // 大小写不敏感：分块扫描并逐字节 tolower 比较
            const size_t CHUNK_SIZE = 1024 * 1024;
            const size_t patternLen = pattern.size();
            std::vector<char> buffer(CHUNK_SIZE + patternLen - 1);
            uintptr_t cur = start;
            while (cur < end) {
                size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
                if (!m_mem.read(cur, buffer.data(), toRead)) {
                    cur += CHUNK_SIZE;
                    continue;
                }
                size_t limit = toRead - patternLen + 1;
                for (size_t i = 0; i < limit; ++i) {
                    bool match = true;
                    for (size_t j = 0; j < patternLen; ++j) {
                        unsigned char a = static_cast<unsigned char>(buffer[i + j]);
                        unsigned char b = static_cast<unsigned char>(pattern[j]);
                        if (std::tolower(a) != b) {
                            match = false;
                            break;
                        }
                    }
                    if (match) results.push_back(cur + i);
                }
                cur += CHUNK_SIZE;
            }
        }
    }
    return results;
}

std::vector<uintptr_t> Search::findStringUTF16(const SearchParams& params,
                                               const std::u16string& str,
                                               bool includeNull,
                                               bool caseSensitive) {
    std::vector<uintptr_t> results;
    if (str.empty()) return results;

    // 构建 pattern 字节序列（小端，每个字符2字节）
    std::vector<uint8_t> pattern;
    for (char16_t ch : str) {
        char16_t out = ch;
        if (!caseSensitive && ch < 128) {
            out = static_cast<char16_t>(std::tolower(static_cast<int>(ch)));
        }
        pattern.push_back(static_cast<uint8_t>(out & 0xFF));
        pattern.push_back(static_cast<uint8_t>((out >> 8) & 0xFF));
    }
    if (includeNull) {
        pattern.push_back(0);
        pattern.push_back(0);
    }

    auto maps = Process::get_process_maps(m_mem.get_pid());
    for (const auto& map : maps) {
        if (!map.isValid()) continue;
        uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
        if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
            continue;
        uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
        uintptr_t end   = std::min(static_cast<uintptr_t>(map.endAddress),   params.endAddress);
        if (start >= end) continue;

        if (caseSensitive) {
            auto addrs = search_bytes(start, end, pattern.data(), pattern.size());
            results.insert(results.end(), addrs.begin(), addrs.end());
        } else {
            // 大小写不敏感（仅对 ASCII 部分有效）
            const size_t CHUNK_SIZE = 1024 * 1024;
            const size_t patternLen = pattern.size();
            std::vector<uint8_t> buffer(CHUNK_SIZE + patternLen - 1);
            uintptr_t cur = start;
            while (cur < end) {
                size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
                if (!m_mem.read(cur, buffer.data(), toRead)) {
                    cur += CHUNK_SIZE;
                    continue;
                }
                size_t limit = toRead - patternLen + 1;
                for (size_t i = 0; i < limit; ++i) {
                    bool match = true;
                    for (size_t j = 0; j < patternLen; j += 2) {
                        uint16_t val = buffer[i + j] | (buffer[i + j + 1] << 8);
                        uint16_t expected = pattern[j] | (pattern[j + 1] << 8);
                        if (!caseSensitive && val < 128 && expected < 128) {
                            val = std::tolower(static_cast<int>(val));
                            expected = std::tolower(static_cast<int>(expected));
                        }
                        if (val != expected) {
                            match = false;
                            break;
                        }
                    }
                    if (match) results.push_back(cur + i);
                }
                cur += CHUNK_SIZE;
            }
        }
    }
    return results;
}


bool Search::parsePatternString(const std::string& patternStr,
                        std::vector<uint8_t>& pattern,
                        std::vector<uint8_t>& mask) {
    pattern.clear();
    mask.clear();
    
    std::string hex;
    for (char ch : patternStr) {
        if (ch == ' ') {
            if (!hex.empty()) {
                // 处理当前 token
                if (hex == "?" || hex == "??") {
                    // 通配符：字节值任意，掩码为 0
                    pattern.push_back(0x00);
                    mask.push_back(0x00);
                } else {
                    // 必须是两位十六进制数
                    if (hex.size() != 2) return false;
                    int val = 0;
                    if (sscanf(hex.c_str(), "%02x", &val) != 1) return false;
                    pattern.push_back(static_cast<uint8_t>(val));
                    mask.push_back(0xFF);
                }
                hex.clear();
            }
        } else {
            hex.push_back(std::tolower(ch));
        }
    }
    // 处理最后一个 token
    if (!hex.empty()) {
        if (hex == "?" || hex == "??") {
            pattern.push_back(0x00);
            mask.push_back(0x00);
        } else {
            if (hex.size() != 2) return false;
            int val = 0;
            if (sscanf(hex.c_str(), "%02x", &val) != 1) return false;
            pattern.push_back(static_cast<uint8_t>(val));
            mask.push_back(0xFF);
        }
    }
    return !pattern.empty();
}

std::vector<uintptr_t> Search::findPattern(const SearchParams& params,
                                           const std::vector<uint8_t>& pattern,
                                           const std::vector<uint8_t>& mask) {
    std::vector<uintptr_t> results;
    if (pattern.empty()) return results;

    // 如果未提供 mask，默认全匹配
    std::vector<uint8_t> effectiveMask = mask;
    if (effectiveMask.empty()) {
        effectiveMask.resize(pattern.size(), 0xFF);
    } else if (effectiveMask.size() != pattern.size()) {
        throw std::invalid_argument("Mask size must equal pattern size");
    }

    auto maps = Process::get_process_maps(m_mem.get_pid());
    for (const auto& map : maps) {
        if (!map.isValid()) continue;
        uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
        if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
            continue;

        uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
        uintptr_t end   = std::min(static_cast<uintptr_t>(map.endAddress),   params.endAddress);
        if (start >= end) continue;

        // 使用分块扫描（类似 search_bytes，但支持通配符）
        const size_t CHUNK_SIZE = 1024 * 1024;
        const size_t patternLen = pattern.size();
        std::vector<uint8_t> buffer(CHUNK_SIZE + patternLen - 1);
        uintptr_t cur = start;
        while (cur < end) {
            size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
            if (!m_mem.read(cur, buffer.data(), toRead)) {
                cur += CHUNK_SIZE;
                continue;
            }
            size_t limit = toRead - patternLen + 1;
            for (size_t i = 0; i < limit; ++i) {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j) {
                    if ((effectiveMask[j] != 0) && (buffer[i + j] != pattern[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    results.push_back(cur + i);
                }
            }
            cur += CHUNK_SIZE;
        }
    }
    return results;
}


std::vector<uintptr_t> Search::scanPattern(const SearchParams& params,
                                           const std::string& patternStr) {
    std::vector<uint8_t> pattern, mask;
    if (!parsePatternString(patternStr, pattern, mask)) {
        return {};
    }
    return findPattern(params, pattern, mask);
}

void Search::scanPatternAsync(const SearchParams& params,
                              const std::string& patternStr,
                              std::function<bool(uintptr_t address)> callback) {
    std::vector<uint8_t> pattern, mask;
    if (!parsePatternString(patternStr, pattern, mask)) {
        return;
    }
    
    auto maps = Process::get_process_maps(m_mem.get_pid());
    const size_t CHUNK_SIZE = 1024 * 1024;
    const size_t patternLen = pattern.size();
    std::vector<uint8_t> buffer(CHUNK_SIZE + patternLen - 1);
    
    for (const auto& map : maps) {
        if (!map.isValid()) continue;
        uint32_t mapMask = static_cast<uint32_t>(map.getMemType());
        if (params.memTypeMask != 0 && (mapMask & params.memTypeMask) == 0)
            continue;
        
        uintptr_t start = std::max(static_cast<uintptr_t>(map.startAddress), params.startAddress);
        uintptr_t end   = std::min(static_cast<uintptr_t>(map.endAddress),   params.endAddress);
        if (start >= end) continue;
        
        uintptr_t cur = start;
        while (cur < end) {
            size_t toRead = std::min(CHUNK_SIZE + patternLen - 1, end - cur);
            if (!m_mem.read(cur, buffer.data(), toRead)) {
                cur += CHUNK_SIZE;
                continue;
            }
            size_t limit = toRead - patternLen + 1;
            for (size_t i = 0; i < limit; ++i) {
                bool match = true;
                for (size_t j = 0; j < patternLen; ++j) {
                    if ((mask[j] != 0) && (buffer[i + j] != pattern[j])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    uintptr_t foundAddr = cur + i;
                    if (!callback(foundAddr)) {
                        return;   // 用户回调返回 false，终止扫描
                    }
                }
            }
            cur += CHUNK_SIZE;
        }
    }
}