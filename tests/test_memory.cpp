#include <gtest/gtest.h>
#include "common/memory.hpp"
#include <cstring>
#include <sys/mman.h>

namespace nixl_topo {
namespace testing {

// =============================================================================
// alloc_buffer Tests
// =============================================================================

TEST(MemoryTest, AllocBuffer_ReturnsNonNull) {
    void* buffer = alloc_buffer(4096, false, false);
    ASSERT_NE(buffer, nullptr);
    free_buffer(buffer, 4096);
}

TEST(MemoryTest, AllocBuffer_ReturnsPageAligned) {
    void* buffer = alloc_buffer(4096, false, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, 4096);
}

TEST(MemoryTest, AllocBuffer_LargerThanPage_StillAligned) {
    size_t size = PAGE_SIZE * 10 + 100;  // Not a multiple of page size
    void* buffer = alloc_buffer(size, false, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, size);
}

TEST(MemoryTest, AllocBuffer_SmallSize_StillAligned) {
    void* buffer = alloc_buffer(64, false, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, 64);
}

TEST(MemoryTest, AllocBuffer_CanWriteAndRead) {
    size_t size = 1024;
    void* buffer = alloc_buffer(size, false, false);
    ASSERT_NE(buffer, nullptr);

    // Write pattern
    std::memset(buffer, 0xAB, size);

    // Verify pattern
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < size; ++i) {
        EXPECT_EQ(bytes[i], 0xAB);
    }

    free_buffer(buffer, size);
}

TEST(MemoryTest, AllocBuffer_WithPinning_ReturnsNonNull) {
    // Note: mlock may fail without sufficient privileges, but alloc_buffer
    // should still succeed (it warns but continues)
    void* buffer = alloc_buffer(4096, true, false);
    ASSERT_NE(buffer, nullptr);

    free_buffer(buffer, 4096);
}

TEST(MemoryTest, AllocBuffer_WithPinning_StillPageAligned) {
    void* buffer = alloc_buffer(8192, true, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, 8192);
}

TEST(MemoryTest, AllocBuffer_LargeAllocation) {
    // 1MB allocation
    size_t size = 1024 * 1024;
    void* buffer = alloc_buffer(size, false, false);
    ASSERT_NE(buffer, nullptr);

    // Write to first and last bytes to verify accessibility
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    bytes[0] = 0x12;
    bytes[size - 1] = 0x34;

    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[size - 1], 0x34);

    free_buffer(buffer, size);
}

TEST(MemoryTest, AllocBuffer_MultipleAllocations) {
    std::vector<void*> buffers;
    const size_t num_buffers = 10;
    const size_t size = 4096;

    // Allocate multiple buffers
    for (size_t i = 0; i < num_buffers; ++i) {
        void* buffer = alloc_buffer(size, false, false);
        ASSERT_NE(buffer, nullptr);
        buffers.push_back(buffer);
    }

    // Verify all are page-aligned and distinct
    for (size_t i = 0; i < num_buffers; ++i) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(buffers[i]);
        EXPECT_EQ(addr % PAGE_SIZE, 0u);

        for (size_t j = i + 1; j < num_buffers; ++j) {
            EXPECT_NE(buffers[i], buffers[j]);
        }
    }

    // Free all
    for (void* buffer : buffers) {
        free_buffer(buffer, size);
    }
}

// =============================================================================
// free_buffer Tests
// =============================================================================

TEST(MemoryTest, FreeBuffer_NullPointer_NoOp) {
    // Should not crash
    free_buffer(nullptr, 1024);
}

TEST(MemoryTest, FreeBuffer_ZeroSize_NoOp) {
    void* buffer = alloc_buffer(4096, false, false);
    ASSERT_NE(buffer, nullptr);

    // Free with zero size - should still work since we call munlock and free
    free_buffer(buffer, 0);
}

TEST(MemoryTest, FreeBuffer_AfterPinnedAlloc) {
    void* buffer = alloc_buffer(4096, true, false);
    ASSERT_NE(buffer, nullptr);

    // Should not crash even if mlock succeeded
    free_buffer(buffer, 4096);
}

// =============================================================================
// PAGE_SIZE Tests
// =============================================================================

TEST(MemoryTest, PageSize_IsReasonable) {
    EXPECT_EQ(PAGE_SIZE, 4096u);
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST(MemoryTest, AllocFree_Repeated) {
    // Allocate and free repeatedly to check for leaks/corruption
    for (int i = 0; i < 100; ++i) {
        void* buffer = alloc_buffer(4096, false, false);
        ASSERT_NE(buffer, nullptr);

        // Write pattern
        std::memset(buffer, i & 0xFF, 4096);

        free_buffer(buffer, 4096);
    }
}

TEST(MemoryTest, AllocFree_VaryingSizes) {
    std::vector<size_t> sizes = {64, 128, 512, 1024, 4096, 8192, 16384, 65536};

    for (size_t size : sizes) {
        void* buffer = alloc_buffer(size, false, false);
        ASSERT_NE(buffer, nullptr) << "Failed for size: " << size;

        uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
        EXPECT_EQ(addr % PAGE_SIZE, 0u) << "Not aligned for size: " << size;

        free_buffer(buffer, size);
    }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(MemoryTest, AllocBuffer_ExactPageSize) {
    void* buffer = alloc_buffer(PAGE_SIZE, false, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, PAGE_SIZE);
}

TEST(MemoryTest, AllocBuffer_OneByteOverPage) {
    size_t size = PAGE_SIZE + 1;
    void* buffer = alloc_buffer(size, false, false);
    ASSERT_NE(buffer, nullptr);

    // Should still be page-aligned (allocation rounds up)
    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, size);
}

TEST(MemoryTest, AllocBuffer_OneByteLessPage) {
    size_t size = PAGE_SIZE - 1;
    void* buffer = alloc_buffer(size, false, false);
    ASSERT_NE(buffer, nullptr);

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer);
    EXPECT_EQ(addr % PAGE_SIZE, 0u);

    free_buffer(buffer, size);
}

} // namespace testing
} // namespace nixl_topo
