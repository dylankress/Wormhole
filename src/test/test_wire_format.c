//
// test_wire_format.c
// Unit tests for wire_format.h little-endian serialization helpers.
//

#include "greatest.h"
#include "../wire_format.h"

GREATEST_MAIN_DEFS();

// ============================================================================
// uint16 tests
// ============================================================================

TEST test_uint16_roundtrip_zero(void)
{
    uint8_t buf[2];
    WriteUint16LE(buf, 0);
    ASSERT_EQ(ReadUint16LE(buf), 0);
    PASS();
}

TEST test_uint16_roundtrip_max(void)
{
    uint8_t buf[2];
    WriteUint16LE(buf, 0xFFFF);
    ASSERT_EQ(ReadUint16LE(buf), 0xFFFF);
    PASS();
}

TEST test_uint16_roundtrip_mid(void)
{
    uint8_t buf[2];
    WriteUint16LE(buf, 0x1234);
    ASSERT_EQ(ReadUint16LE(buf), 0x1234);
    PASS();
}

TEST test_uint16_byte_order(void)
{
    uint8_t buf[2];
    WriteUint16LE(buf, 0x0102);
    // Little-endian: least significant byte first
    ASSERT_EQ(buf[0], 0x02);
    ASSERT_EQ(buf[1], 0x01);
    PASS();
}

// ============================================================================
// uint32 tests
// ============================================================================

TEST test_uint32_roundtrip_zero(void)
{
    uint8_t buf[4];
    WriteUint32LE(buf, 0);
    ASSERT_EQ(ReadUint32LE(buf), 0u);
    PASS();
}

TEST test_uint32_roundtrip_max(void)
{
    uint8_t buf[4];
    WriteUint32LE(buf, 0xFFFFFFFFu);
    ASSERT_EQ(ReadUint32LE(buf), 0xFFFFFFFFu);
    PASS();
}

TEST test_uint32_roundtrip_mid(void)
{
    uint8_t buf[4];
    WriteUint32LE(buf, 0x12345678u);
    ASSERT_EQ(ReadUint32LE(buf), 0x12345678u);
    PASS();
}

TEST test_uint32_byte_order(void)
{
    uint8_t buf[4];
    WriteUint32LE(buf, 0x01020304u);
    ASSERT_EQ(buf[0], 0x04);
    ASSERT_EQ(buf[1], 0x03);
    ASSERT_EQ(buf[2], 0x02);
    ASSERT_EQ(buf[3], 0x01);
    PASS();
}

// ============================================================================
// uint64 tests
// ============================================================================

TEST test_uint64_roundtrip_zero(void)
{
    uint8_t buf[8];
    WriteUint64LE(buf, 0);
    ASSERT_EQ(ReadUint64LE(buf), 0ULL);
    PASS();
}

TEST test_uint64_roundtrip_max(void)
{
    uint8_t buf[8];
    WriteUint64LE(buf, 0xFFFFFFFFFFFFFFFFULL);
    ASSERT_EQ(ReadUint64LE(buf), 0xFFFFFFFFFFFFFFFFULL);
    PASS();
}

TEST test_uint64_roundtrip_mid(void)
{
    uint8_t buf[8];
    WriteUint64LE(buf, 0x0123456789ABCDEFULL);
    ASSERT_EQ(ReadUint64LE(buf), 0x0123456789ABCDEFULL);
    PASS();
}

TEST test_uint64_byte_order(void)
{
    uint8_t buf[8];
    WriteUint64LE(buf, 0x0102030405060708ULL);
    ASSERT_EQ(buf[0], 0x08);
    ASSERT_EQ(buf[1], 0x07);
    ASSERT_EQ(buf[2], 0x06);
    ASSERT_EQ(buf[3], 0x05);
    ASSERT_EQ(buf[4], 0x04);
    ASSERT_EQ(buf[5], 0x03);
    ASSERT_EQ(buf[6], 0x02);
    ASSERT_EQ(buf[7], 0x01);
    PASS();
}

// ============================================================================
// Suites
// ============================================================================

SUITE(uint16_suite)
{
    RUN_TEST(test_uint16_roundtrip_zero);
    RUN_TEST(test_uint16_roundtrip_max);
    RUN_TEST(test_uint16_roundtrip_mid);
    RUN_TEST(test_uint16_byte_order);
}

SUITE(uint32_suite)
{
    RUN_TEST(test_uint32_roundtrip_zero);
    RUN_TEST(test_uint32_roundtrip_max);
    RUN_TEST(test_uint32_roundtrip_mid);
    RUN_TEST(test_uint32_byte_order);
}

SUITE(uint64_suite)
{
    RUN_TEST(test_uint64_roundtrip_zero);
    RUN_TEST(test_uint64_roundtrip_max);
    RUN_TEST(test_uint64_roundtrip_mid);
    RUN_TEST(test_uint64_byte_order);
}

int main(void)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(uint16_suite);
    RUN_SUITE(uint32_suite);
    RUN_SUITE(uint64_suite);
    GREATEST_MAIN_END();
}
