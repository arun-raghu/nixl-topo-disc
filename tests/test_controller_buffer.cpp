#include <gtest/gtest.h>
#include "controller/controller_buffer.hpp"
#include "common/types.hpp"
#include <cstring>

namespace nixl_topo {
namespace testing {

// =============================================================================
// ControllerBuffer Allocation Tests
// =============================================================================

TEST(ControllerBufferTest, DefaultConstruction_NotAllocated) {
    ControllerBuffer buffer;
    EXPECT_FALSE(buffer.is_allocated());
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.num_agents(), 0u);
}

TEST(ControllerBufferTest, Allocate_Success) {
    ControllerBuffer buffer;
    EXPECT_TRUE(buffer.allocate(4));
    EXPECT_TRUE(buffer.is_allocated());
    EXPECT_NE(buffer.data(), nullptr);
    EXPECT_GT(buffer.size(), 0u);
    EXPECT_EQ(buffer.num_agents(), 4u);
}

TEST(ControllerBufferTest, Allocate_ZeroAgents_Fails) {
    ControllerBuffer buffer;
    EXPECT_FALSE(buffer.allocate(0));
    EXPECT_FALSE(buffer.is_allocated());
}

TEST(ControllerBufferTest, Allocate_DoubleAllocation_Fails) {
    ControllerBuffer buffer;
    EXPECT_TRUE(buffer.allocate(4));
    EXPECT_FALSE(buffer.allocate(8));  // Should fail, already allocated
    EXPECT_EQ(buffer.num_agents(), 4u);  // Original allocation unchanged
}

TEST(ControllerBufferTest, Allocate_PageAligned) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    uintptr_t addr = reinterpret_cast<uintptr_t>(buffer.data());
    EXPECT_EQ(addr % 4096, 0u);  // Page-aligned
}

TEST(ControllerBufferTest, Deallocate_ClearsState) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    buffer.deallocate();

    EXPECT_FALSE(buffer.is_allocated());
    EXPECT_EQ(buffer.data(), nullptr);
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.num_agents(), 0u);
}

TEST(ControllerBufferTest, Deallocate_Idempotent) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    buffer.deallocate();
    buffer.deallocate();  // Should not crash

    EXPECT_FALSE(buffer.is_allocated());
}

TEST(ControllerBufferTest, Destructor_Deallocates) {
    void* data_ptr = nullptr;
    {
        ControllerBuffer buffer;
        ASSERT_TRUE(buffer.allocate(4));
        data_ptr = buffer.data();
        EXPECT_NE(data_ptr, nullptr);
    }
    // Buffer destroyed, memory freed (can't easily verify, but should not crash)
}

// =============================================================================
// BufferHeader Tests
// =============================================================================

TEST(ControllerBufferTest, Header_NotNull) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_NE(buffer.header(), nullptr);
}

TEST(ControllerBufferTest, Header_ConstNotNull) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    const ControllerBuffer& const_buffer = buffer;
    EXPECT_NE(const_buffer.header(), nullptr);
}

TEST(ControllerBufferTest, Header_NullWhenNotAllocated) {
    ControllerBuffer buffer;
    EXPECT_EQ(buffer.header(), nullptr);
}

TEST(ControllerBufferTest, Header_MagicInWireFormat) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    // Header is stored in wire format (magic is 32-bit)
    EXPECT_EQ(buffer.header()->magic, to_wire32(BufferHeader::MAGIC));
}

TEST(ControllerBufferTest, Header_VersionInWireFormat) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_EQ(buffer.header()->version, to_wire32(BufferHeader::VERSION));
}

TEST(ControllerBufferTest, Header_NumAgentsInWireFormat) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(8));

    EXPECT_EQ(buffer.header()->num_agents, to_wire32(8));
}

TEST(ControllerBufferTest, Header_OffsetsInWireFormat) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    // Offsets should be positive values in wire format
    uint32_t agent_offset = from_wire32(buffer.header()->agent_slots_offset);
    uint32_t notif_offset = from_wire32(buffer.header()->notification_offset);

    EXPECT_GT(agent_offset, 0u);
    EXPECT_GT(notif_offset, agent_offset);
}

TEST(ControllerBufferTest, Header_BufferBaseAddr) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    uint64_t stored_addr = from_wire64(buffer.header()->buffer_base_addr);
    uint64_t actual_addr = reinterpret_cast<uint64_t>(buffer.data());

    EXPECT_EQ(stored_addr, actual_addr);
}

TEST(ControllerBufferTest, Header_ReadyFlagInitiallyZero) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_EQ(buffer.header()->ready_flag, to_wire64(0));
}

// =============================================================================
// AgentSlot Tests
// =============================================================================

TEST(ControllerBufferTest, AgentSlot_ValidIndex) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_NE(buffer.agent_slot(i), nullptr);
    }
}

TEST(ControllerBufferTest, AgentSlot_ConstValidIndex) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    const ControllerBuffer& const_buffer = buffer;
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_NE(const_buffer.agent_slot(i), nullptr);
    }
}

TEST(ControllerBufferTest, AgentSlot_InvalidIndex) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_EQ(buffer.agent_slot(4), nullptr);
    EXPECT_EQ(buffer.agent_slot(100), nullptr);
    EXPECT_EQ(buffer.agent_slot(UINT32_MAX), nullptr);
}

TEST(ControllerBufferTest, AgentSlot_NullWhenNotAllocated) {
    ControllerBuffer buffer;
    EXPECT_EQ(buffer.agent_slot(0), nullptr);
}

TEST(ControllerBufferTest, AgentSlot_DistinctAddresses) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    std::vector<const void*> addresses;
    for (uint32_t i = 0; i < 4; ++i) {
        addresses.push_back(buffer.agent_slot(i));
    }

    // All addresses should be distinct
    for (size_t i = 0; i < addresses.size(); ++i) {
        for (size_t j = i + 1; j < addresses.size(); ++j) {
            EXPECT_NE(addresses[i], addresses[j]);
        }
    }
}

TEST(ControllerBufferTest, AgentSlot_ProperSpacing) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 3; ++i) {
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(buffer.agent_slot(i));
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(buffer.agent_slot(i + 1));
        EXPECT_EQ(addr2 - addr1, AGENT_SLOT_SIZE);
    }
}

TEST(ControllerBufferTest, AgentSlot_InitiallyNotPopulated) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_FALSE(buffer.is_agent_registered(i));
    }
}

// =============================================================================
// NotificationSlot Tests
// =============================================================================

TEST(ControllerBufferTest, NotificationSlot_ValidIndex) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_NE(buffer.notification_slot(i), nullptr);
    }
}

TEST(ControllerBufferTest, NotificationSlot_InvalidIndex) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_EQ(buffer.notification_slot(4), nullptr);
    EXPECT_EQ(buffer.notification_slot(100), nullptr);
}

TEST(ControllerBufferTest, NotificationSlot_NullWhenNotAllocated) {
    ControllerBuffer buffer;
    EXPECT_EQ(buffer.notification_slot(0), nullptr);
}

TEST(ControllerBufferTest, NotificationSlot_ProperSpacing) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 3; ++i) {
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(buffer.notification_slot(i));
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(buffer.notification_slot(i + 1));
        EXPECT_EQ(addr2 - addr1, NOTIFICATION_SLOT_SIZE);
    }
}

// =============================================================================
// Agent Registration Tests
// =============================================================================

TEST(ControllerBufferTest, IsAgentRegistered_InitiallyFalse) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_FALSE(buffer.is_agent_registered(i));
    }
}

TEST(ControllerBufferTest, IsAgentRegistered_AfterPopulation) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    // Simulate agent registration by setting populated_flag in wire format
    AgentSlot* slot = buffer.agent_slot(2);
    ASSERT_NE(slot, nullptr);
    slot->populated_flag = to_wire64(1);

    EXPECT_FALSE(buffer.is_agent_registered(0));
    EXPECT_FALSE(buffer.is_agent_registered(1));
    EXPECT_TRUE(buffer.is_agent_registered(2));
    EXPECT_FALSE(buffer.is_agent_registered(3));
}

TEST(ControllerBufferTest, IsAgentRegistered_InvalidIndex_ReturnsFalse) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_FALSE(buffer.is_agent_registered(4));
    EXPECT_FALSE(buffer.is_agent_registered(100));
}

TEST(ControllerBufferTest, IsAgentRegistered_NotAllocated_ReturnsFalse) {
    ControllerBuffer buffer;
    EXPECT_FALSE(buffer.is_agent_registered(0));
}

// =============================================================================
// Ready Flag Tests
// =============================================================================

TEST(ControllerBufferTest, SetReadyFlag_SetsWireFormat) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    EXPECT_EQ(buffer.header()->ready_flag, to_wire64(0));

    buffer.set_ready_flag();

    EXPECT_EQ(buffer.header()->ready_flag, to_wire64(1));
}

TEST(ControllerBufferTest, SetReadyFlag_Idempotent) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    buffer.set_ready_flag();
    buffer.set_ready_flag();

    EXPECT_EQ(buffer.header()->ready_flag, to_wire64(1));
}

TEST(ControllerBufferTest, SetReadyFlag_NotAllocated_NoOp) {
    ControllerBuffer buffer;
    buffer.set_ready_flag();  // Should not crash
    EXPECT_EQ(buffer.header(), nullptr);
}

// =============================================================================
// Move Semantics Tests
// =============================================================================

TEST(ControllerBufferTest, MoveConstruction) {
    ControllerBuffer buffer1;
    ASSERT_TRUE(buffer1.allocate(4));
    void* original_data = buffer1.data();
    size_t original_size = buffer1.size();

    ControllerBuffer buffer2(std::move(buffer1));

    // buffer2 should have the data
    EXPECT_EQ(buffer2.data(), original_data);
    EXPECT_EQ(buffer2.size(), original_size);
    EXPECT_EQ(buffer2.num_agents(), 4u);
    EXPECT_TRUE(buffer2.is_allocated());

    // buffer1 should be empty
    EXPECT_EQ(buffer1.data(), nullptr);
    EXPECT_EQ(buffer1.size(), 0u);
    EXPECT_FALSE(buffer1.is_allocated());
}

TEST(ControllerBufferTest, MoveAssignment) {
    ControllerBuffer buffer1;
    ASSERT_TRUE(buffer1.allocate(4));
    void* original_data = buffer1.data();

    ControllerBuffer buffer2;
    buffer2 = std::move(buffer1);

    EXPECT_EQ(buffer2.data(), original_data);
    EXPECT_TRUE(buffer2.is_allocated());
    EXPECT_FALSE(buffer1.is_allocated());
}

TEST(ControllerBufferTest, MoveAssignment_DeallocatesExisting) {
    ControllerBuffer buffer1;
    ASSERT_TRUE(buffer1.allocate(4));

    ControllerBuffer buffer2;
    ASSERT_TRUE(buffer2.allocate(8));

    buffer2 = std::move(buffer1);

    // buffer2 should have buffer1's data (4 agents, not 8)
    EXPECT_EQ(buffer2.num_agents(), 4u);
}

TEST(ControllerBufferTest, SelfMoveAssignment_NoOp) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));
    void* original_data = buffer.data();

    buffer = std::move(buffer);

    // Should be unchanged
    EXPECT_EQ(buffer.data(), original_data);
    EXPECT_TRUE(buffer.is_allocated());
}

// =============================================================================
// Buffer Layout Tests
// =============================================================================

TEST(ControllerBufferTest, BufferLayout_HeaderFirst) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    // Header should be at the start of the buffer
    EXPECT_EQ(buffer.header(), buffer.data());
}

TEST(ControllerBufferTest, BufferLayout_AgentSlotsAfterHeader) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    uintptr_t header_addr = reinterpret_cast<uintptr_t>(buffer.header());
    uintptr_t slot0_addr = reinterpret_cast<uintptr_t>(buffer.agent_slot(0));

    // First agent slot should be right after header
    EXPECT_EQ(slot0_addr - header_addr, sizeof(BufferHeader));
}

TEST(ControllerBufferTest, BufferLayout_NotificationsAfterSlots) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    uintptr_t last_slot_addr = reinterpret_cast<uintptr_t>(buffer.agent_slot(3));
    uintptr_t first_notif_addr = reinterpret_cast<uintptr_t>(buffer.notification_slot(0));

    // First notification slot should be after last agent slot
    EXPECT_EQ(first_notif_addr - last_slot_addr, AGENT_SLOT_SIZE);
}

TEST(ControllerBufferTest, BufferSize_MatchesExpected) {
    ControllerBuffer buffer;
    ASSERT_TRUE(buffer.allocate(4));

    size_t expected_size = sizeof(BufferHeader) +
                          4 * AGENT_SLOT_SIZE +
                          4 * NOTIFICATION_SLOT_SIZE +
                          4 * TEST_RESULT_SIZE +
                          CONTROLLER_CMD_AREA_SIZE;

    EXPECT_EQ(buffer.size(), expected_size);
}

// =============================================================================
// Different Agent Counts
// =============================================================================

TEST(ControllerBufferTest, Allocate_SingleAgent) {
    ControllerBuffer buffer;
    EXPECT_TRUE(buffer.allocate(1));
    EXPECT_EQ(buffer.num_agents(), 1u);
    EXPECT_NE(buffer.agent_slot(0), nullptr);
    EXPECT_EQ(buffer.agent_slot(1), nullptr);
}

TEST(ControllerBufferTest, Allocate_ManyAgents) {
    ControllerBuffer buffer;
    EXPECT_TRUE(buffer.allocate(100));
    EXPECT_EQ(buffer.num_agents(), 100u);

    // Check first, middle, and last slots
    EXPECT_NE(buffer.agent_slot(0), nullptr);
    EXPECT_NE(buffer.agent_slot(50), nullptr);
    EXPECT_NE(buffer.agent_slot(99), nullptr);
    EXPECT_EQ(buffer.agent_slot(100), nullptr);
}

} // namespace testing
} // namespace nixl_topo
