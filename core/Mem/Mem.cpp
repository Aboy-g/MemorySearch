#include "Mem.hpp"
#include "../Keystone/includes/keystone.h"
#include <cstdio>
#include <unistd.h>

Mem::Mem() {
    set_pid(getpid());
    m_io.open(pid);
}

Mem::Mem(int pid) {
    set_pid(pid);
    m_io.open(pid);
}

Mem::~Mem() = default;  // ProcIO 自动 close

bool Mem::read(uintptr_t address, void *buffer, size_t size) {
    return m_io.read(address, buffer, size);      // pvm → pread
}

bool Mem::write(uintptr_t address, const void *buffer, size_t size) {
    return m_io.write(address, buffer, size);     // pvm → pwrite
}

// ============================================================
// Keystone 汇编注入 (不变)
// ============================================================

bool Mem::write_assembly(uintptr_t address, const std::vector<std::string> &instructions) {
    if (instructions.empty() || pid < 0) return false;

    std::string asm_code;
    for (const auto &line : instructions) {
        if (!asm_code.empty()) asm_code += "; ";
        asm_code += line;
    }

    ks_engine *ks = nullptr;
    ks_arch arch;
    ks_mode mode;

#if defined(__aarch64__)
    arch = KS_ARCH_ARM64; mode = KS_MODE_LITTLE_ENDIAN;
#elif defined(__arm__)
    arch = KS_ARCH_ARM;   mode = KS_MODE_ARM;
#elif defined(__i386__)
    arch = KS_ARCH_X86;   mode = KS_MODE_32;
#elif defined(__x86_64__)
    arch = KS_ARCH_X86;   mode = KS_MODE_64;
#endif

    if (ks_open(arch, mode, &ks) != KS_ERR_OK) {
        fprintf(stderr, "Keystone: ks_open failed\n");
        return false;
    }

    unsigned char *encode = nullptr;
    size_t size = 0, stat_count = 0;
    if (ks_asm(ks, asm_code.c_str(), address, &encode, &size, &stat_count) != KS_ERR_OK) {
        fprintf(stderr, "Keystone: ks_asm failed: %s\n", ks_strerror(ks_errno(ks)));
        ks_close(ks);
        return false;
    }

    bool ok = write(address, encode, size);
    ks_free(encode);
    ks_close(ks);
    return ok;
}
