#ifndef SIMD_HPP
#define SIMD_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

// ============================================================
// SIMD 加速工具集 — ARM64 NEON / x86 SSE / 通用 fallback
// ============================================================

namespace SIMD {

// ----------------------------------------------------------
// 快速 memcmp — 比较两块内存是否相等 (返回 true 表示相等)
// ----------------------------------------------------------
inline bool fast_memcmp(const uint8_t* a, const uint8_t* b, size_t size) {
#if defined(__aarch64__)
    // NEON: 一次比较 16 字节
    while (size >= 16) {
        uint8x16_t va = vld1q_u8(a);
        uint8x16_t vb = vld1q_u8(b);
        uint8x16_t cmp = vceqq_u8(va, vb);
        // vminvq_u8 检查是否所有字节相等
        uint64_t mask_low = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
        uint64_t mask_high = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
        if ((mask_low & mask_high) != 0xFFFFFFFFFFFFFFFFULL)
            return false;
        a += 16; b += 16; size -= 16;
    }
#elif defined(__x86_64__) || defined(__i386__)
    // SSE2: 一次比较 16 字节
    while (size >= 16) {
        __m128i va = _mm_loadu_si128((const __m128i*)a);
        __m128i vb = _mm_loadu_si128((const __m128i*)b);
        __m128i cmp = _mm_cmpeq_epi8(va, vb);
        if (_mm_movemask_epi8(cmp) != 0xFFFF)
            return false;
        a += 16; b += 16; size -= 16;
    }
#endif
    return memcmp(a, b, size) == 0;
}

// ----------------------------------------------------------
// 快速查找字节 — 在 buffer 中查找第一个匹配的字节, 返回索引 (未找到返回 -1)
// ----------------------------------------------------------
inline ptrdiff_t find_byte(const uint8_t* buffer, size_t bufSize, uint8_t target) {
#if defined(__aarch64__)
    uint8x16_t vt = vdupq_n_u8(target);
    size_t i = 0;
    for (; i + 16 <= bufSize; i += 16) {
        uint8x16_t vbuf = vld1q_u8(buffer + i);
        uint8x16_t cmp = vceqq_u8(vbuf, vt);
        uint64x2_t bits = vreinterpretq_u64_u8(cmp);
        if (vgetq_lane_u64(bits, 0) || vgetq_lane_u64(bits, 1)) {
            // 找到，精确位置
            for (size_t j = 0; j < 16; j++) {
                if (buffer[i + j] == target) return static_cast<ptrdiff_t>(i + j);
            }
        }
    }
    // 尾部
    for (; i < bufSize; i++) {
        if (buffer[i] == target) return static_cast<ptrdiff_t>(i);
    }
    return -1;
#elif defined(__x86_64__) || defined(__i386__)
    // 使用 SSE 或者 fallback
    const uint8_t* pos = (const uint8_t*)memchr(buffer, target, bufSize);
    return pos ? (pos - buffer) : -1;
#else
    const uint8_t* pos = (const uint8_t*)memchr(buffer, target, bufSize);
    return pos ? (pos - buffer) : -1;
#endif
}

// ----------------------------------------------------------
// 快速 memchr 多次 — 在 buffer 中找出所有等于 target 的位置
// out 是输出 buffer, maxOut 是最大输出数, 返回实际找到数
// ----------------------------------------------------------
inline size_t find_bytes(const uint8_t* buffer, size_t bufSize,
                         uint8_t target, uintptr_t* out, size_t maxOut) {
    size_t count = 0;
    size_t i = 0;

#if defined(__aarch64__)
    uint8x16_t vt = vdupq_n_u8(target);
    for (; i + 16 <= bufSize && count < maxOut; i += 16) {
        uint8x16_t vbuf = vld1q_u8(buffer + i);
        uint8x16_t cmp = vceqq_u8(vbuf, vt);
        uint64x2_t bits = vreinterpretq_u64_u8(cmp);
        uint64_t low = vgetq_lane_u64(bits, 0);
        uint64_t high = vgetq_lane_u64(bits, 1);
        // 展开检查
        if (low) {
            for (size_t j = 0; j < 8; j++) {
                if (low & (1ULL << (j * 8))) {
                    out[count++] = i + j;
                    if (count >= maxOut) return count;
                }
            }
        }
        if (high) {
            for (size_t j = 0; j < 8; j++) {
                if (high & (1ULL << (j * 8))) {
                    out[count++] = i + 8 + j;
                    if (count >= maxOut) return count;
                }
            }
        }
    }
#endif
    for (; i < bufSize && count < maxOut; i++) {
        if (buffer[i] == target) out[count++] = i;
    }
    return count;
}

// ----------------------------------------------------------
// 快速 4字节值搜索 — 搜索 int32/float 值，返回匹配偏移量数组
// align: 是否按4字节对齐搜索；step: 搜索步长
// ----------------------------------------------------------
inline size_t find_value_u32(const uint8_t* buffer, size_t bufSize,
                              uint32_t value, uintptr_t* out, size_t maxOut,
                              bool align = true) {
    size_t count = 0;
    size_t step = align ? 4 : 1;

#if defined(__aarch64__)
    uint32x4_t vt = vdupq_n_u32(value);
    size_t i = 0;
    for (; i + 16 <= bufSize && count < maxOut; i += step) {
        if (align && (i % 4 != 0)) continue;
        if (i + 16 > bufSize) break;
        uint32x4_t vbuf = vld1q_u32((const uint32_t*)(buffer + i));
        uint32x4_t cmp = vceqq_u32(vbuf, vt);
        uint64x2_t bits = vreinterpretq_u64_u32(cmp);
        uint64_t low = vgetq_lane_u64(bits, 0);
        uint64_t high = vgetq_lane_u64(bits, 1);
        if (low) {
            for (size_t j = 0; j < 2; j++) {
                if (low & (0xFFFFFFFFULL << (j * 32))) {
                    out[count++] = i + j * 4;
                    if (count >= maxOut) return count;
                }
            }
        }
        if (high) {
            for (size_t j = 0; j < 2; j++) {
                if (high & (0xFFFFFFFFULL << (j * 32))) {
                    out[count++] = i + 8 + j * 4;
                    if (count >= maxOut) return count;
                }
            }
        }
    }
#endif

    // 通用 fallback
    const uint32_t* buf32 = (const uint32_t*)(buffer);
    size_t maxIdx = (bufSize - 3) / 4;
    for (size_t idx = i / 4; idx <= maxIdx && count < maxOut; idx += (step / 4)) {
        if (buf32[idx] == value) {
            out[count++] = idx * 4;
        }
    }
    return count;
}

// ----------------------------------------------------------
// 快速 8字节值搜索 — 搜索 int64/double 值
// ----------------------------------------------------------
inline size_t find_value_u64(const uint8_t* buffer, size_t bufSize,
                              uint64_t value, uintptr_t* out, size_t maxOut,
                              bool align = true) {
    size_t count = 0;
    size_t step = align ? 8 : 1;

#if defined(__aarch64__)
    uint64x2_t vt = vdupq_n_u64(value);
    size_t i = 0;
    for (; i + 16 <= bufSize && count < maxOut; i += step) {
        if (align && (i % 8 != 0)) continue;
        if (i + 16 > bufSize) break;
        uint64x2_t vbuf = vld1q_u64((const uint64_t*)(buffer + i));
        uint64x2_t cmp = vceqq_u64(vbuf, vt);
        if (vgetq_lane_u64(cmp, 0)) {
            out[count++] = i;
            if (count >= maxOut) return count;
        }
        if (vgetq_lane_u64(cmp, 1)) {
            out[count++] = i + 8;
            if (count >= maxOut) return count;
        }
    }
#endif

    const uint64_t* buf64 = (const uint64_t*)(buffer);
    size_t maxIdx = (bufSize - 7) / 8;
    for (size_t idx = i / 8; idx <= maxIdx && count < maxOut; idx += (step / 8)) {
        if (buf64[idx] == value) {
            out[count++] = idx * 8;
        }
    }
    return count;
}

// ----------------------------------------------------------
// 模式搜索 (朴素，但内循环展开) — 用于短模式
// ----------------------------------------------------------
inline size_t find_pattern_naive(const uint8_t* buffer, size_t bufSize,
                                  const uint8_t* pattern, size_t patLen,
                                  const uint8_t* mask,
                                  uintptr_t* out, size_t maxOut) {
    size_t count = 0;
    if (patLen == 0 || bufSize < patLen) return 0;

    size_t limit = bufSize - patLen + 1;
    if (mask) {
        for (size_t i = 0; i < limit && count < maxOut; i++) {
            bool match = true;
            for (size_t j = 0; j < patLen; j++) {
                if (mask[j] && buffer[i + j] != pattern[j]) {
                    match = false; break;
                }
            }
            if (match) out[count++] = i;
        }
    } else {
        for (size_t i = 0; i < limit && count < maxOut; i++) {
            if (memcmp(buffer + i, pattern, patLen) == 0) {
                out[count++] = i;
            }
        }
    }
    return count;
}

} // namespace SIMD

#endif // SIMD_HPP
