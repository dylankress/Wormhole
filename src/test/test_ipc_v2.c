//
// test_ipc_v2.c
// Unit tests for IPC v2 protocol — structured errors, operation registry, constants.
//

#include "../ipc.h"
#include "../wire_format.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>

GREATEST_MAIN_DEFS();

// ============================================================================
// error_response_suite — IpcWriteErrorResponse / IpcReadErrorResponse roundtrip
// ============================================================================

TEST test_write_read_ok_status(void)
{
    uint8_t buf[256];
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_OK, NULL);
    ASSERT_EQ(written, 1u);
    ASSERT_EQ(buf[0], IPC_STATUS_OK);

    uint8_t status = 0xFF;
    char msg[128] = "dirty";
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg, sizeof(msg));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_OK);
    ASSERT_EQ(msg[0], '\0');
    PASS();
}

TEST test_write_read_error_with_message(void)
{
    uint8_t buf[256];
    const char *error_msg = "file not found";
    uint16_t msg_len = (uint16_t)strlen(error_msg);
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_ERROR, error_msg);
    ASSERT_EQ(written, (uint32_t)(1 + 2 + msg_len));

    // Verify wire format manually: [1B status][2B LE msg_len][msg]
    ASSERT_EQ(buf[0], IPC_STATUS_ERROR);
    ASSERT_EQ(ReadUint16LE(buf + 1), msg_len);
    ASSERT(memcmp(buf + 3, error_msg, msg_len) == 0);

    // Roundtrip read
    uint8_t status = 0xFF;
    char msg[128];
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg, sizeof(msg));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_ERROR);
    ASSERT(strcmp(msg, "file not found") == 0);
    PASS();
}

TEST test_write_error_truncates_long_message(void)
{
    // Build a message longer than IPC_MAX_ERROR_MSG_LEN (512)
    char long_msg[600];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';

    uint8_t buf[1024];
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_ERROR, long_msg);
    ASSERT_EQ(written, (uint32_t)(1 + 2 + IPC_MAX_ERROR_MSG_LEN));

    uint16_t stored_len = ReadUint16LE(buf + 1);
    ASSERT_EQ(stored_len, (uint16_t)IPC_MAX_ERROR_MSG_LEN);

    // Read back
    uint8_t status;
    char msg_out[600];
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg_out, sizeof(msg_out));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_ERROR);
    ASSERT_EQ((uint32_t)strlen(msg_out), (uint32_t)IPC_MAX_ERROR_MSG_LEN);
    for (int i = 0; i < IPC_MAX_ERROR_MSG_LEN; i++)
        ASSERT_EQ(msg_out[i], 'A');
    PASS();
}

TEST test_write_error_null_buf_returns_zero(void)
{
    uint32_t written = IpcWriteErrorResponse(NULL, 0, IPC_STATUS_ERROR, "test");
    ASSERT_EQ(written, 0u);
    PASS();
}

TEST test_write_error_null_msg_writes_status_only(void)
{
    uint8_t buf[16];
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_ERROR, NULL);
    ASSERT_EQ(written, 1u);
    ASSERT_EQ(buf[0], IPC_STATUS_ERROR);
    PASS();
}

TEST test_write_error_capacity_too_small(void)
{
    uint8_t buf[4]; // Room for status byte but not full status+len+msg
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_ERROR, "hello world");
    // Falls back to status-only
    ASSERT_EQ(written, 1u);
    ASSERT_EQ(buf[0], IPC_STATUS_ERROR);
    PASS();
}

TEST test_read_ok_ignores_trailing_message(void)
{
    // Manually build OK status with a trailing message in the buffer.
    // The reader should ignore the message for OK status.
    uint8_t buf[64];
    const char *msg = "ignored";
    buf[0] = IPC_STATUS_OK;
    WriteUint16LE(buf + 1, (uint16_t)strlen(msg));
    memcpy(buf + 3, msg, strlen(msg));
    uint32_t total = (uint32_t)(3 + strlen(msg));

    uint8_t status;
    char msg_out[64] = "dirty";
    BOOLEAN ok = IpcReadErrorResponse(buf, total, &status, msg_out, sizeof(msg_out));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_OK);
    ASSERT_EQ(msg_out[0], '\0');
    PASS();
}

TEST test_read_error_small_msg_buffer(void)
{
    uint8_t buf[256];
    const char *error_msg = "a longer error message for truncation testing";
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_ERROR, error_msg);
    ASSERT(written > 3);

    uint8_t status;
    char msg_out[10]; // Only 10 bytes — will be truncated to 9 chars + null
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg_out, sizeof(msg_out));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_ERROR);
    ASSERT_EQ(strlen(msg_out), 9u);
    ASSERT(memcmp(msg_out, error_msg, 9) == 0);
    PASS();
}

TEST test_read_null_buf_returns_false(void)
{
    uint8_t status;
    char msg[32];
    BOOLEAN ok = IpcReadErrorResponse(NULL, 0, &status, msg, sizeof(msg));
    ASSERT_FALSE(ok);
    PASS();
}

TEST test_read_zero_size_returns_false(void)
{
    uint8_t buf[1] = { IPC_STATUS_OK };
    uint8_t status;
    char msg[32];
    BOOLEAN ok = IpcReadErrorResponse(buf, 0, &status, msg, sizeof(msg));
    ASSERT_FALSE(ok);
    PASS();
}

TEST test_write_read_not_found_status(void)
{
    uint8_t buf[256];
    const char *error_msg = "no such file";
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_NOT_FOUND, error_msg);
    ASSERT(written > 1);

    uint8_t status;
    char msg[128];
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg, sizeof(msg));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_NOT_FOUND);
    ASSERT(strcmp(msg, "no such file") == 0);
    PASS();
}

TEST test_write_read_cancelled_status(void)
{
    uint8_t buf[256];
    const char *error_msg = "operation cancelled";
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_CANCELLED, error_msg);

    uint8_t status;
    char msg[128];
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg, sizeof(msg));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_CANCELLED);
    ASSERT(strcmp(msg, "operation cancelled") == 0);
    PASS();
}

TEST test_write_read_busy_status(void)
{
    uint8_t buf[256];
    const char *error_msg = "too many operations";
    uint32_t written = IpcWriteErrorResponse(buf, sizeof(buf), IPC_STATUS_BUSY, error_msg);

    uint8_t status;
    char msg[128];
    BOOLEAN ok = IpcReadErrorResponse(buf, written, &status, msg, sizeof(msg));
    ASSERT(ok);
    ASSERT_EQ(status, IPC_STATUS_BUSY);
    ASSERT(strcmp(msg, "too many operations") == 0);
    PASS();
}

SUITE(error_response_suite)
{
    RUN_TEST(test_write_read_ok_status);
    RUN_TEST(test_write_read_error_with_message);
    RUN_TEST(test_write_error_truncates_long_message);
    RUN_TEST(test_write_error_null_buf_returns_zero);
    RUN_TEST(test_write_error_null_msg_writes_status_only);
    RUN_TEST(test_write_error_capacity_too_small);
    RUN_TEST(test_read_ok_ignores_trailing_message);
    RUN_TEST(test_read_error_small_msg_buffer);
    RUN_TEST(test_read_null_buf_returns_false);
    RUN_TEST(test_read_zero_size_returns_false);
    RUN_TEST(test_write_read_not_found_status);
    RUN_TEST(test_write_read_cancelled_status);
    RUN_TEST(test_write_read_busy_status);
}

// ============================================================================
// operation_registry_suite — IpcServer_RegisterOp / GetOp / UnregisterOp
// ============================================================================

// Use high op_ids to avoid collision with other possible callers
#define TEST_OP_BASE 10000

static void op_registry_setup(void *arg)
{
    (void)arg;
    // Clean up any leftover ops from previous tests
    for (uint32_t i = TEST_OP_BASE; i < TEST_OP_BASE + IPC_MAX_OPERATIONS + 256; i++)
        IpcServer_UnregisterOp(i);
}

TEST test_register_and_get(void)
{
    op_registry_setup(NULL);
    uint32_t op_id = TEST_OP_BASE + 1;
    ASSERT(IpcServer_RegisterOp(op_id, IPC_CMD_STORE));

    OPERATION_PROGRESS *op = IpcServer_GetOp(op_id);
    ASSERT(op != NULL);
    ASSERT_EQ(op->op_id, op_id);
    ASSERT_EQ(op->command, IPC_CMD_STORE);
    ASSERT(op->active);
    ASSERT_EQ(op->cancelled, 0);
    ASSERT_EQ(op->bytes_done, 0ULL);
    ASSERT_EQ(op->bytes_total, 0ULL);
    ASSERT_EQ(op->chunks_done, 0u);
    ASSERT_EQ(op->chunks_total, 0u);

    IpcServer_UnregisterOp(op_id);
    PASS();
}

TEST test_get_nonexistent_returns_null(void)
{
    op_registry_setup(NULL);
    OPERATION_PROGRESS *op = IpcServer_GetOp(0xDEADBEEF);
    ASSERT(op == NULL);
    PASS();
}

TEST test_unregister_removes_op(void)
{
    op_registry_setup(NULL);
    uint32_t op_id = TEST_OP_BASE + 2;
    ASSERT(IpcServer_RegisterOp(op_id, IPC_CMD_GET));
    ASSERT(IpcServer_GetOp(op_id) != NULL);

    IpcServer_UnregisterOp(op_id);
    ASSERT(IpcServer_GetOp(op_id) == NULL);
    PASS();
}

TEST test_cancel_operation(void)
{
    op_registry_setup(NULL);
    uint32_t op_id = TEST_OP_BASE + 3;
    ASSERT(IpcServer_RegisterOp(op_id, IPC_CMD_STORE));
    ASSERT_FALSE(IpcServer_IsOpCancelled(op_id));

    // Set cancelled flag via pointer (same path as server's CANCEL handler)
    OPERATION_PROGRESS *op = IpcServer_GetOp(op_id);
    ASSERT(op != NULL);
    WH_ATOMIC_SET(&op->cancelled, 1);

    ASSERT(IpcServer_IsOpCancelled(op_id));

    IpcServer_UnregisterOp(op_id);
    PASS();
}

TEST test_cancel_nonexistent_returns_false(void)
{
    op_registry_setup(NULL);
    ASSERT_FALSE(IpcServer_IsOpCancelled(0xBAADF00D));
    PASS();
}

TEST test_registry_full_then_free_slot(void)
{
    op_registry_setup(NULL);
    // Fill all IPC_MAX_OPERATIONS slots
    for (uint32_t i = 0; i < IPC_MAX_OPERATIONS; i++) {
        ASSERT(IpcServer_RegisterOp(TEST_OP_BASE + 100 + i, IPC_CMD_STATUS));
    }

    // Next registration should fail
    ASSERT_FALSE(IpcServer_RegisterOp(TEST_OP_BASE + 100 + IPC_MAX_OPERATIONS, IPC_CMD_STATUS));

    // Unregister one slot
    IpcServer_UnregisterOp(TEST_OP_BASE + 100);

    // Now registration should succeed in the freed slot
    ASSERT(IpcServer_RegisterOp(TEST_OP_BASE + 200, IPC_CMD_STATUS));

    OPERATION_PROGRESS *op = IpcServer_GetOp(TEST_OP_BASE + 200);
    ASSERT(op != NULL);
    ASSERT_EQ(op->op_id, TEST_OP_BASE + 200);

    // Clean up
    for (uint32_t i = 1; i < IPC_MAX_OPERATIONS; i++)
        IpcServer_UnregisterOp(TEST_OP_BASE + 100 + i);
    IpcServer_UnregisterOp(TEST_OP_BASE + 200);
    PASS();
}

TEST test_unregister_nonexistent_is_noop(void)
{
    op_registry_setup(NULL);
    // Should not crash or corrupt state
    IpcServer_UnregisterOp(0xCAFEBABE);
    IpcServer_UnregisterOp(0);
    PASS();
}

TEST test_register_different_commands(void)
{
    op_registry_setup(NULL);
    uint32_t id1 = TEST_OP_BASE + 50;
    uint32_t id2 = TEST_OP_BASE + 51;

    ASSERT(IpcServer_RegisterOp(id1, IPC_CMD_SEND));
    ASSERT(IpcServer_RegisterOp(id2, IPC_CMD_RECEIVE));

    OPERATION_PROGRESS *op1 = IpcServer_GetOp(id1);
    OPERATION_PROGRESS *op2 = IpcServer_GetOp(id2);
    ASSERT(op1 != NULL);
    ASSERT(op2 != NULL);
    ASSERT_EQ(op1->command, IPC_CMD_SEND);
    ASSERT_EQ(op2->command, IPC_CMD_RECEIVE);
    // They should be distinct entries
    ASSERT(op1 != op2);

    IpcServer_UnregisterOp(id1);
    IpcServer_UnregisterOp(id2);
    PASS();
}

TEST test_register_sets_start_time(void)
{
    op_registry_setup(NULL);
    uint32_t op_id = TEST_OP_BASE + 60;
    ASSERT(IpcServer_RegisterOp(op_id, IPC_CMD_STORE));

    OPERATION_PROGRESS *op = IpcServer_GetOp(op_id);
    ASSERT(op != NULL);
    ASSERT(op->start_time > 0.0);

    IpcServer_UnregisterOp(op_id);
    PASS();
}

SUITE(operation_registry_suite)
{
    RUN_TEST(test_register_and_get);
    RUN_TEST(test_get_nonexistent_returns_null);
    RUN_TEST(test_unregister_removes_op);
    RUN_TEST(test_cancel_operation);
    RUN_TEST(test_cancel_nonexistent_returns_false);
    RUN_TEST(test_registry_full_then_free_slot);
    RUN_TEST(test_unregister_nonexistent_is_noop);
    RUN_TEST(test_register_different_commands);
    RUN_TEST(test_register_sets_start_time);
}

// ============================================================================
// constants_suite — v2 framing, event masks, payload sizes, command codes
// ============================================================================

TEST test_protocol_versions(void)
{
    ASSERT_EQ(IPC_PROTOCOL_VERSION_1, 1);
    ASSERT_EQ(IPC_PROTOCOL_VERSION_2, 2);
    ASSERT_EQ(IPC_PROTOCOL_VERSION, IPC_PROTOCOL_VERSION_2);
    PASS();
}

TEST test_v2_framing_constants(void)
{
    ASSERT_EQ(IPC_V2_MAGIC, 0x0002);
    ASSERT_EQ(IPC_V2_HEADER_SIZE, 10);
    ASSERT_EQ(IPC_V1_HEADER_SIZE, 4);
    // v2 body = [1B cmd][4B op_id][payload] => min body = 5
    // v2 wire = [2B version][4B body_len][body] => header overhead = 6
    // IPC_V2_HEADER_SIZE accounts for version+length+cmd+op_id minus body_len overlap
    PASS();
}

TEST test_event_mask_bit_positions(void)
{
    // Masks should be single-bit values at the correct positions
    ASSERT_EQ(IPC_EVENT_MASK_PROGRESS,    (1u << 0));
    ASSERT_EQ(IPC_EVENT_MASK_OP_COMPLETE, (1u << 1));
    ASSERT_EQ(IPC_EVENT_MASK_PEER_CHANGE, (1u << 2));
    ASSERT_EQ(IPC_EVENT_MASK_FILE_STATUS, (1u << 3));
    ASSERT_EQ(IPC_EVENT_MASK_HEALTH,      (1u << 4));
    ASSERT_EQ(IPC_EVENT_MASK_TRANSFER,    (1u << 5));
    PASS();
}

TEST test_event_mask_all_covers_all_events(void)
{
    uint32_t combined = IPC_EVENT_MASK_PROGRESS | IPC_EVENT_MASK_OP_COMPLETE |
                        IPC_EVENT_MASK_PEER_CHANGE | IPC_EVENT_MASK_FILE_STATUS |
                        IPC_EVENT_MASK_HEALTH | IPC_EVENT_MASK_TRANSFER;
    ASSERT_EQ(IPC_EVENT_MASK_ALL, combined);
    ASSERT_EQ(IPC_EVENT_MASK_ALL, 0x3Fu);
    PASS();
}

TEST test_event_mask_matches_event_type(void)
{
    // IpcServer_PushEvent uses: mask_bit = (1 << (event_type - 1))
    // Verify this mapping is consistent for all event types.
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_PROGRESS - 1)),    IPC_EVENT_MASK_PROGRESS);
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_OP_COMPLETE - 1)), IPC_EVENT_MASK_OP_COMPLETE);
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_PEER_CHANGE - 1)), IPC_EVENT_MASK_PEER_CHANGE);
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_FILE_STATUS - 1)), IPC_EVENT_MASK_FILE_STATUS);
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_HEALTH - 1)),      IPC_EVENT_MASK_HEALTH);
    ASSERT_EQ((uint32_t)(1 << (IPC_EVENT_TRANSFER - 1)),    IPC_EVENT_MASK_TRANSFER);
    PASS();
}

TEST test_progress_payload_size(void)
{
    // Progress: [4B op_id][8B bytes_done][8B bytes_total][4B chunks_done]
    //           [4B chunks_total][4B speed_bps][4B eta_sec]
    uint32_t expected = 4 + 8 + 8 + 4 + 4 + 4 + 4;
    ASSERT_EQ((uint32_t)IPC_PROGRESS_PAYLOAD_SIZE, expected);
    ASSERT_EQ((uint32_t)IPC_PROGRESS_PAYLOAD_SIZE, 36u);
    PASS();
}

TEST test_status_codes_distinct(void)
{
    ASSERT_EQ(IPC_STATUS_OK,        0x00);
    ASSERT_EQ(IPC_STATUS_ERROR,     0x01);
    ASSERT_EQ(IPC_STATUS_NOT_FOUND, 0x02);
    ASSERT_EQ(IPC_STATUS_CANCELLED, 0x03);
    ASSERT_EQ(IPC_STATUS_BUSY,      0x04);
    PASS();
}

TEST test_v2_command_codes(void)
{
    // Phase 8A commands
    ASSERT_EQ(IPC_CMD_SUBSCRIBE,       0x0C);
    ASSERT_EQ(IPC_CMD_CANCEL,          0x0D);
    ASSERT_EQ(IPC_CMD_EVENT,           0xE0);
    // Phase 8C transfer commands
    ASSERT_EQ(IPC_CMD_SEND,            0x0E);
    ASSERT_EQ(IPC_CMD_RECEIVE,         0x0F);
    ASSERT_EQ(IPC_CMD_TRANSFER_STATUS, 0x10);
    ASSERT_EQ(IPC_CMD_TRANSFER_LIST,   0x11);
    // Phase 8D config commands
    ASSERT_EQ(IPC_CMD_CONFIG_LIST,     0x12);
    ASSERT_EQ(IPC_CMD_CONFIG_GET,      0x13);
    ASSERT_EQ(IPC_CMD_CONFIG_SET,      0x14);
    // Phase 8E lifecycle
    ASSERT_EQ(IPC_CMD_HEARTBEAT,       0x15);
    PASS();
}

TEST test_legacy_command_codes_unchanged(void)
{
    // v1 commands should be preserved for backward compatibility
    ASSERT_EQ(IPC_CMD_STORE,       0x01);
    ASSERT_EQ(IPC_CMD_GET,         0x02);
    ASSERT_EQ(IPC_CMD_STATUS,      0x03);
    ASSERT_EQ(IPC_CMD_SHUTDOWN,    0x04);
    ASSERT_EQ(IPC_CMD_DHT_STATUS,  0x05);
    ASSERT_EQ(IPC_CMD_LIST_FILES,  0x06);
    ASSERT_EQ(IPC_CMD_FILE_GET,    0x07);
    ASSERT_EQ(IPC_CMD_FILE_DELETE, 0x08);
    ASSERT_EQ(IPC_CMD_EXPORT_KEY,  0x09);
    ASSERT_EQ(IPC_CMD_IMPORT_KEY,  0x0A);
    ASSERT_EQ(IPC_CMD_PEER_LIST,   0x0B);
    PASS();
}

TEST test_max_operations_and_subscribers(void)
{
    ASSERT_EQ(IPC_MAX_OPERATIONS, 16);
    ASSERT_EQ(IPC_MAX_SUBSCRIBERS, 8);
    PASS();
}

TEST test_operation_progress_struct_layout(void)
{
    // Verify the struct can hold max-range values without truncation
    OPERATION_PROGRESS op;
    memset(&op, 0, sizeof(op));
    op.op_id = UINT32_MAX;
    op.bytes_done = UINT64_MAX;
    op.bytes_total = UINT64_MAX;
    op.chunks_done = UINT32_MAX;
    op.chunks_total = UINT32_MAX;
    op.command = 0xFF;
    op.active = TRUE;
    op.cancelled = 1;

    ASSERT_EQ(op.op_id, UINT32_MAX);
    ASSERT_EQ(op.bytes_done, UINT64_MAX);
    ASSERT_EQ(op.bytes_total, UINT64_MAX);
    ASSERT_EQ(op.chunks_done, UINT32_MAX);
    ASSERT_EQ(op.chunks_total, UINT32_MAX);
    ASSERT_EQ(op.command, 0xFF);
    ASSERT(op.active);
    ASSERT_EQ(op.cancelled, 1);
    PASS();
}

TEST test_max_error_msg_len(void)
{
    ASSERT_EQ(IPC_MAX_ERROR_MSG_LEN, 512);
    PASS();
}

SUITE(constants_suite)
{
    RUN_TEST(test_protocol_versions);
    RUN_TEST(test_v2_framing_constants);
    RUN_TEST(test_event_mask_bit_positions);
    RUN_TEST(test_event_mask_all_covers_all_events);
    RUN_TEST(test_event_mask_matches_event_type);
    RUN_TEST(test_progress_payload_size);
    RUN_TEST(test_status_codes_distinct);
    RUN_TEST(test_v2_command_codes);
    RUN_TEST(test_legacy_command_codes_unchanged);
    RUN_TEST(test_max_operations_and_subscribers);
    RUN_TEST(test_operation_progress_struct_layout);
    RUN_TEST(test_max_error_msg_len);
}

// ============================================================================
// main
// ============================================================================

int main(void)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(error_response_suite);
    RUN_SUITE(operation_registry_suite);
    RUN_SUITE(constants_suite);
    GREATEST_MAIN_END();
}
