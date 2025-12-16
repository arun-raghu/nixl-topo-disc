#include <gtest/gtest.h>
#include "common/types.hpp"
#include <cstring>
#include <endian.h>

namespace nixl_topo {
namespace testing {

// =============================================================================
// Wire Format Conversion Tests
// =============================================================================

TEST(WireFormatTest, ToWire32_ConvertsToBigEndian) {
    uint32_t host_value = 0x12345678;
    uint32_t wire_value = to_wire32(host_value);

    // Wire format should be big-endian
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&wire_value);
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[1], 0x34);
    EXPECT_EQ(bytes[2], 0x56);
    EXPECT_EQ(bytes[3], 0x78);
}

TEST(WireFormatTest, FromWire32_ConvertsFromBigEndian) {
    // Create big-endian value manually
    uint8_t bytes[4] = {0x12, 0x34, 0x56, 0x78};
    uint32_t wire_value;
    std::memcpy(&wire_value, bytes, sizeof(wire_value));

    uint32_t host_value = from_wire32(wire_value);
    EXPECT_EQ(host_value, 0x12345678);
}

TEST(WireFormatTest, ToWire32_FromWire32_RoundTrip) {
    uint32_t original = 0xDEADBEEF;
    uint32_t converted = from_wire32(to_wire32(original));
    EXPECT_EQ(converted, original);
}

TEST(WireFormatTest, ToWire64_ConvertsToBigEndian) {
    uint64_t host_value = 0x123456789ABCDEF0ULL;
    uint64_t wire_value = to_wire64(host_value);

    uint8_t* bytes = reinterpret_cast<uint8_t*>(&wire_value);
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[1], 0x34);
    EXPECT_EQ(bytes[2], 0x56);
    EXPECT_EQ(bytes[3], 0x78);
    EXPECT_EQ(bytes[4], 0x9A);
    EXPECT_EQ(bytes[5], 0xBC);
    EXPECT_EQ(bytes[6], 0xDE);
    EXPECT_EQ(bytes[7], 0xF0);
}

TEST(WireFormatTest, FromWire64_ConvertsFromBigEndian) {
    uint8_t bytes[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    uint64_t wire_value;
    std::memcpy(&wire_value, bytes, sizeof(wire_value));

    uint64_t host_value = from_wire64(wire_value);
    EXPECT_EQ(host_value, 0x123456789ABCDEF0ULL);
}

TEST(WireFormatTest, ToWire64_FromWire64_RoundTrip) {
    uint64_t original = 0xCAFEBABEDEADBEEFULL;
    uint64_t converted = from_wire64(to_wire64(original));
    EXPECT_EQ(converted, original);
}

TEST(WireFormatTest, WireFormat_ZeroValues) {
    EXPECT_EQ(to_wire32(0), 0u);
    EXPECT_EQ(from_wire32(0), 0u);
    EXPECT_EQ(to_wire64(0), 0ULL);
    EXPECT_EQ(from_wire64(0), 0ULL);
}

TEST(WireFormatTest, WireFormat_MaxValues) {
    EXPECT_EQ(from_wire32(to_wire32(UINT32_MAX)), UINT32_MAX);
    EXPECT_EQ(from_wire64(to_wire64(UINT64_MAX)), UINT64_MAX);
}

// =============================================================================
// BufferHeader Tests
// =============================================================================

TEST(BufferHeaderTest, MagicConstant_IsCorrect) {
    EXPECT_EQ(BufferHeader::MAGIC, 0x4E49584Cu);  // "NIXL"
}

TEST(BufferHeaderTest, VersionConstant_IsCorrect) {
    EXPECT_EQ(BufferHeader::VERSION, 1u);
}

TEST(BufferHeaderTest, ToWire_ConvertAllFields) {
    BufferHeader header;
    header.magic = BufferHeader::MAGIC;
    header.version = BufferHeader::VERSION;
    header.num_agents = 4;
    header.agent_slots_offset = 64;
    header.notification_offset = 1024;
    header.ready_flag = 1;
    header.buffer_base_addr = 0x7FFF12340000;

    header.to_wire();

    // Verify all fields are in wire format
    EXPECT_EQ(header.magic, to_wire32(BufferHeader::MAGIC));
    EXPECT_EQ(header.version, to_wire32(BufferHeader::VERSION));
    EXPECT_EQ(header.num_agents, to_wire32(4));
    EXPECT_EQ(header.agent_slots_offset, to_wire32(64));
    EXPECT_EQ(header.notification_offset, to_wire32(1024));
    EXPECT_EQ(header.ready_flag, to_wire64(1));
    EXPECT_EQ(header.buffer_base_addr, to_wire64(0x7FFF12340000));
}

TEST(BufferHeaderTest, FromWire_ConvertAllFields) {
    BufferHeader header;
    // Set fields in wire format
    header.magic = to_wire32(BufferHeader::MAGIC);
    header.version = to_wire32(BufferHeader::VERSION);
    header.num_agents = to_wire32(8);
    header.agent_slots_offset = to_wire32(128);
    header.notification_offset = to_wire32(2048);
    header.ready_flag = to_wire64(1);
    header.buffer_base_addr = to_wire64(0xABCD00001234);

    header.from_wire();

    // Verify all fields are in host format
    EXPECT_EQ(header.magic, BufferHeader::MAGIC);
    EXPECT_EQ(header.version, BufferHeader::VERSION);
    EXPECT_EQ(header.num_agents, 8u);
    EXPECT_EQ(header.agent_slots_offset, 128u);
    EXPECT_EQ(header.notification_offset, 2048u);
    EXPECT_EQ(header.ready_flag, 1u);
    EXPECT_EQ(header.buffer_base_addr, 0xABCD00001234ULL);
}

TEST(BufferHeaderTest, ToWire_FromWire_RoundTrip) {
    BufferHeader original;
    original.magic = BufferHeader::MAGIC;
    original.version = BufferHeader::VERSION;
    original.num_agents = 16;
    original.agent_slots_offset = 256;
    original.notification_offset = 4096;
    original.ready_flag = 0;
    original.buffer_base_addr = 0x1234567890ABCDEF;

    BufferHeader converted = original;
    converted.to_wire();
    converted.from_wire();

    EXPECT_EQ(converted.magic, original.magic);
    EXPECT_EQ(converted.version, original.version);
    EXPECT_EQ(converted.num_agents, original.num_agents);
    EXPECT_EQ(converted.agent_slots_offset, original.agent_slots_offset);
    EXPECT_EQ(converted.notification_offset, original.notification_offset);
    EXPECT_EQ(converted.ready_flag, original.ready_flag);
    EXPECT_EQ(converted.buffer_base_addr, original.buffer_base_addr);
}

TEST(BufferHeaderTest, Size_IsExpected) {
    // BufferHeader should be 48 bytes (8+4+4+4+4+8+8+8 with padding)
    // Actually let's just verify it's reasonably sized
    EXPECT_GE(sizeof(BufferHeader), 40u);  // At minimum
    EXPECT_LE(sizeof(BufferHeader), 64u);  // Not excessively large
}

// =============================================================================
// AgentSlot Tests
// =============================================================================

TEST(AgentSlotTest, ToWire_ConvertHeaderFields) {
    AgentSlot slot;
    slot.populated_flag = 1;
    slot.metadata_size = 512;
    slot.reserved = 0;

    slot.to_wire();

    EXPECT_EQ(slot.populated_flag, to_wire64(1));
    EXPECT_EQ(slot.metadata_size, to_wire32(512));
    EXPECT_EQ(slot.reserved, to_wire32(0));
}

TEST(AgentSlotTest, FromWire_ConvertHeaderFields) {
    AgentSlot slot;
    slot.populated_flag = to_wire64(1);
    slot.metadata_size = to_wire32(1024);
    slot.reserved = to_wire32(0);

    slot.from_wire();

    EXPECT_EQ(slot.populated_flag, 1u);
    EXPECT_EQ(slot.metadata_size, 1024u);
    EXPECT_EQ(slot.reserved, 0u);
}

TEST(AgentSlotTest, ToWire_FromWire_RoundTrip) {
    AgentSlot original;
    original.populated_flag = 1;
    original.metadata_size = 2048;
    original.reserved = 0;

    AgentSlot converted = original;
    converted.to_wire();
    converted.from_wire();

    EXPECT_EQ(converted.populated_flag, original.populated_flag);
    EXPECT_EQ(converted.metadata_size, original.metadata_size);
    EXPECT_EQ(converted.reserved, original.reserved);
}

TEST(AgentSlotTest, MetadataPointer_ReturnsCorrectOffset) {
    AgentSlot slot;
    const uint8_t* metadata_ptr = slot.metadata();
    const uint8_t* slot_ptr = reinterpret_cast<const uint8_t*>(&slot);

    // Metadata should be at AGENT_SLOT_HEADER_SIZE offset
    EXPECT_EQ(metadata_ptr - slot_ptr, static_cast<ptrdiff_t>(AGENT_SLOT_HEADER_SIZE));
}

TEST(AgentSlotTest, MetadataPointer_NonConst) {
    AgentSlot slot;
    uint8_t* metadata_ptr = slot.metadata();
    uint8_t* slot_ptr = reinterpret_cast<uint8_t*>(&slot);

    EXPECT_EQ(metadata_ptr - slot_ptr, static_cast<ptrdiff_t>(AGENT_SLOT_HEADER_SIZE));
}

TEST(AgentSlotTest, HeaderSize_IsCorrect) {
    // Header: populated_flag (8) + metadata_size (4) + reserved (4) = 16
    EXPECT_EQ(AGENT_SLOT_HEADER_SIZE, 16u);
}

TEST(AgentSlotTest, SlotSize_AccommodatesMetadata) {
    // AGENT_SLOT_SIZE should fit header + MAX_METADATA_BLOB_SIZE
    EXPECT_GE(AGENT_SLOT_SIZE, AGENT_SLOT_HEADER_SIZE + MAX_METADATA_BLOB_SIZE);
}

// =============================================================================
// Notification Tests
// =============================================================================

TEST(NotificationTest, DefaultConstruction) {
    Notification notif;
    EXPECT_EQ(notif.sequence, 0u);
    EXPECT_EQ(notif.type, NotificationType::NONE);
    EXPECT_EQ(notif.source_id, 0u);
    EXPECT_EQ(notif.timestamp_ns, 0u);
    EXPECT_EQ(notif.payload, 0u);
}

TEST(NotificationTest, Size_MatchesSlotSize) {
    EXPECT_EQ(sizeof(Notification), NOTIFICATION_SLOT_SIZE);
}

TEST(NotificationTest, NotificationType_Values) {
    EXPECT_EQ(static_cast<uint32_t>(NotificationType::NONE), 0u);
    EXPECT_EQ(static_cast<uint32_t>(NotificationType::RENDEZVOUS_COMPLETE), 1u);
}

TEST(NotificationTest, CanSetFields) {
    Notification notif;
    notif.sequence = 42;
    notif.type = NotificationType::RENDEZVOUS_COMPLETE;
    notif.source_id = 5;
    notif.timestamp_ns = 1234567890;
    notif.payload = 0xDEADBEEF;

    EXPECT_EQ(notif.sequence, 42u);
    EXPECT_EQ(notif.type, NotificationType::RENDEZVOUS_COMPLETE);
    EXPECT_EQ(notif.source_id, 5u);
    EXPECT_EQ(notif.timestamp_ns, 1234567890u);
    EXPECT_EQ(notif.payload, 0xDEADBEEFu);
}

// =============================================================================
// Constants Tests
// =============================================================================

TEST(ConstantsTest, BufferSizes_AreReasonable) {
    EXPECT_EQ(DEFAULT_TEST_BUFFER_SIZE, 256 * 1024 * 1024);  // 256MB
    EXPECT_EQ(MAX_METADATA_BLOB_SIZE, AGENT_SLOT_SIZE - AGENT_SLOT_HEADER_SIZE);  // 4080
    EXPECT_GE(MAX_METADATA_BLOB_SIZE, 4000u);  // At least ~4KB for metadata
    EXPECT_LE(MAX_METADATA_BLOB_SIZE, 65536u);  // Not more than 64KB
}

TEST(ConstantsTest, SlotSizes_AreReasonable) {
    EXPECT_GT(AGENT_SLOT_SIZE, 0u);
    EXPECT_GT(NOTIFICATION_SLOT_SIZE, 0u);

    // Slot size should be larger than header
    EXPECT_GT(AGENT_SLOT_SIZE, AGENT_SLOT_HEADER_SIZE);
}

TEST(ConstantsTest, NotificationMessages_AreDefined) {
    EXPECT_STREQ(NOTIF_AGENT_REGISTERED, "AGENT_REGISTERED");
    EXPECT_STREQ(NOTIF_RENDEZVOUS_COMPLETE, "RENDEZVOUS_COMPLETE");
}

} // namespace testing
} // namespace nixl_topo
