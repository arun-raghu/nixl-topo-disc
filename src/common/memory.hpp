#pragma once

#include <cstdlib>
#include <cstddef>
#include <sys/mman.h>
#include <iostream>

namespace nixl_topo {

constexpr size_t PAGE_SIZE = 4096;

/// Allocate page-aligned memory, optionally pinned for zero-copy RDMA.
/// @param size Size in bytes to allocate
/// @param pin_memory If true, attempt to pin memory with mlock
/// @param warn_on_no_pin If true and pin_memory is false, print warning
/// @return Pointer to allocated memory, or nullptr on failure
inline void* alloc_buffer(size_t size, bool pin_memory, bool warn_on_no_pin = true) {
    // Align size to page boundary
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Allocate page-aligned memory
    void* ptr = nullptr;
    if (posix_memalign(&ptr, PAGE_SIZE, aligned_size) != 0) {
        return nullptr;
    }

    if (pin_memory) {
        // Pin memory to prevent swapping (enables zero-copy RDMA)
        if (mlock(ptr, aligned_size) != 0) {
            std::cerr << "Warning: mlock failed (run as root or increase RLIMIT_MEMLOCK). "
                      << "Zero-copy RDMA may not be optimal.\n";
            // Continue anyway - UCX will handle it, just less efficiently
        }
    } else if (warn_on_no_pin) {
        std::cerr << "Warning: Memory pinning disabled. Zero-copy RDMA may not be optimal.\n";
    }

    return ptr;
}

/// Free buffer allocated with alloc_buffer.
/// @param ptr Pointer to buffer
/// @param size Original size passed to alloc_buffer
inline void free_buffer(void* ptr, size_t size) {
    if (ptr) {
        size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        munlock(ptr, aligned_size);  // No-op if not locked
        std::free(ptr);
    }
}

} // namespace nixl_topo
