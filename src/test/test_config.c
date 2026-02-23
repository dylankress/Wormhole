//
// test_config.c
// Unit tests for config.c — INI parsing, get/set, file round-trip.
//

#include "../config.h"
#include "greatest.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

GREATEST_MAIN_DEFS();

// ============================================================================
// Test environment — redirect config paths to a temporary directory
// ============================================================================

static char g_test_home[512];

#ifdef _WIN32
static char g_env_buf[544];
#endif

static void setup_test_home(void)
{
#ifdef _WIN32
    char temp[512];
    GetTempPathA(sizeof(temp), temp);
    snprintf(g_test_home, sizeof(g_test_home), "%swh_test_cfg_%lu",
             temp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(g_test_home, NULL);

    snprintf(g_env_buf, sizeof(g_env_buf), "USERPROFILE=%s", g_test_home);
    _putenv(g_env_buf);
#else
    snprintf(g_test_home, sizeof(g_test_home), "/tmp/wh_test_cfg_%d", (int)getpid());
    mkdir(g_test_home, 0755);
    setenv("HOME", g_test_home, 1);
#endif
}

static void cleanup_test_home(void)
{
    char cmd[600];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" >nul 2>&1", g_test_home);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", g_test_home);
#endif
    system(cmd);
}

// Helper: build a path within the test home directory.
static void make_temp_path(char *out, size_t out_len, const char *filename)
{
#ifdef _WIN32
    snprintf(out, out_len, "%s\\%s", g_test_home, filename);
#else
    snprintf(out, out_len, "%s/%s", g_test_home, filename);
#endif
}

// ============================================================================
// defaults_suite — loading defaults when no config file exists
// ============================================================================

TEST test_load_missing_file(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "nonexistent_config.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    // Verify defaults
    ASSERT_EQ(Config_GetUint64(config, "max_storage_gb", 0), (uint64_t)CONFIG_DEFAULT_MAX_STORAGE_GB);
    ASSERT_EQ(Config_GetUint64(config, "replication_target", 0), (uint64_t)CONFIG_DEFAULT_REPLICATION_TARGET);
    ASSERT(strcmp(Config_GetString(config, "relay_host", ""), CONFIG_DEFAULT_RELAY_HOST) == 0);
    ASSERT_EQ(Config_GetUint64(config, "relay_port", 0), (uint64_t)CONFIG_DEFAULT_RELAY_PORT);

    Config_Destroy(config);
    PASS();
}

TEST test_default_count(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "nonexistent_config2.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);
    ASSERT_EQ(config->count, 14u);

    Config_Destroy(config);
    PASS();
}

// ============================================================================
// getset_suite — in-memory get/set operations
// ============================================================================

TEST test_set_and_get_string(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(Config_Set(config, "foo", "bar"));
    ASSERT(strcmp(Config_GetString(config, "foo", ""), "bar") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_set_and_get_uint64(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test2.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(Config_SetUint64(config, "num", 42));
    ASSERT_EQ(Config_GetUint64(config, "num", 0), 42ULL);

    Config_Destroy(config);
    PASS();
}

TEST test_get_missing_key(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test3.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(strcmp(Config_GetString(config, "nonexistent_key", "fallback"), "fallback") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_get_uint64_invalid(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test4.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(Config_Set(config, "x", "notanumber"));
    ASSERT_EQ(Config_GetUint64(config, "x", 99), 99ULL);

    Config_Destroy(config);
    PASS();
}

TEST test_overwrite_existing(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test5.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(Config_Set(config, "key1", "first"));
    ASSERT(Config_Set(config, "key1", "second"));
    ASSERT(strcmp(Config_GetString(config, "key1", ""), "second") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_case_insensitive(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test6.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    // Defaults set "relay_host" = "wormholerelay.com"
    // Overwrite via different case
    ASSERT(Config_Set(config, "Relay_Host", "test.example.com"));
    ASSERT(strcmp(Config_GetString(config, "relay_host", ""), "test.example.com") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_config_full(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "gs_test7.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    // Defaults set 14 entries. Fill remaining 50 slots.
    for (uint32_t i = 0; i < 50; i++)
    {
        char key[32];
        snprintf(key, sizeof(key), "testkey_%u", i);
        ASSERT(Config_Set(config, key, "value"));
    }

    ASSERT_EQ(config->count, 64u);

    // Next Set should fail
    ASSERT_FALSE(Config_Set(config, "overflow_key", "overflow_value"));

    Config_Destroy(config);
    PASS();
}

// ============================================================================
// file_roundtrip_suite — save/load from actual files
// ============================================================================

TEST test_save_and_reload(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "roundtrip.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    ASSERT(Config_Set(config, "custom_key", "custom_value"));
    ASSERT(Config_SetUint64(config, "custom_num", 12345));
    ASSERT(Config_Save(config));
    Config_Destroy(config);

    // Reload and verify
    WORMHOLE_CONFIG *config2 = Config_Load(path);
    ASSERT(config2 != NULL);
    ASSERT(strcmp(Config_GetString(config2, "custom_key", ""), "custom_value") == 0);
    ASSERT_EQ(Config_GetUint64(config2, "custom_num", 0), 12345ULL);

    // Defaults should still be present
    ASSERT_EQ(Config_GetUint64(config2, "max_storage_gb", 0), (uint64_t)CONFIG_DEFAULT_MAX_STORAGE_GB);

    Config_Destroy(config2);
    PASS();
}

TEST test_parse_comments(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "comments.ini");

    // Write file with comment lines
    FILE *fh = fopen(path, "w");
    ASSERT(fh != NULL);
    fprintf(fh, "# This is a hash comment\n");
    fprintf(fh, "; This is a semicolon comment\n");
    fprintf(fh, "valid_key = valid_value\n");
    fprintf(fh, "# Another comment\n");
    fprintf(fh, "another_key = another_value\n");
    fclose(fh);

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);
    ASSERT(strcmp(Config_GetString(config, "valid_key", ""), "valid_value") == 0);
    ASSERT(strcmp(Config_GetString(config, "another_key", ""), "another_value") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_parse_whitespace(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "whitespace.ini");

    // Write file with extra whitespace
    FILE *fh = fopen(path, "w");
    ASSERT(fh != NULL);
    fprintf(fh, "  spaced_key  =  spaced_value  \n");
    fclose(fh);

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);
    ASSERT(strcmp(Config_GetString(config, "spaced_key", ""), "spaced_value") == 0);

    Config_Destroy(config);
    PASS();
}

TEST test_parse_empty_lines(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "emptylines.ini");

    // Write file with blank lines between entries
    FILE *fh = fopen(path, "w");
    ASSERT(fh != NULL);
    fprintf(fh, "\n");
    fprintf(fh, "key_a = value_a\n");
    fprintf(fh, "\n");
    fprintf(fh, "\n");
    fprintf(fh, "key_b = value_b\n");
    fprintf(fh, "\n");
    fclose(fh);

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);
    ASSERT(strcmp(Config_GetString(config, "key_a", ""), "value_a") == 0);
    ASSERT(strcmp(Config_GetString(config, "key_b", ""), "value_b") == 0);

    Config_Destroy(config);
    PASS();
}

// ============================================================================
// validation_suite — bounds checking for config values
// ============================================================================

TEST test_validate_numeric_in_range(void)
{
    ASSERT(Config_ValidateValue("max_storage_gb", "1"));
    ASSERT(Config_ValidateValue("max_storage_gb", "1048576"));
    ASSERT(Config_ValidateValue("max_storage_gb", "100"));
    ASSERT(Config_ValidateValue("replication_target", "4"));
    ASSERT(Config_ValidateValue("dht_port", "4568"));
    ASSERT(Config_ValidateValue("relay_port", "443"));
    ASSERT(Config_ValidateValue("min_storage_ratio", "0"));
    ASSERT(Config_ValidateValue("min_storage_ratio", "100"));
    PASS();
}

TEST test_validate_numeric_out_of_range(void)
{
    ASSERT_FALSE(Config_ValidateValue("max_storage_gb", "0"));
    ASSERT_FALSE(Config_ValidateValue("max_storage_gb", "1048577"));
    ASSERT_FALSE(Config_ValidateValue("replication_target", "0"));
    ASSERT_FALSE(Config_ValidateValue("replication_target", "17"));
    ASSERT_FALSE(Config_ValidateValue("dht_port", "1023"));
    ASSERT_FALSE(Config_ValidateValue("dht_port", "65536"));
    ASSERT_FALSE(Config_ValidateValue("relay_port", "0"));
    ASSERT_FALSE(Config_ValidateValue("min_storage_ratio", "101"));
    ASSERT_FALSE(Config_ValidateValue("ec_data_shards", "1"));
    ASSERT_FALSE(Config_ValidateValue("ec_parity_shards", "0"));
    ASSERT_FALSE(Config_ValidateValue("health_check_interval_sec", "59"));
    ASSERT_FALSE(Config_ValidateValue("proof_cache_count", "0"));
    ASSERT_FALSE(Config_ValidateValue("proof_cache_count", "257"));
    PASS();
}

TEST test_validate_numeric_not_a_number(void)
{
    ASSERT_FALSE(Config_ValidateValue("max_storage_gb", "abc"));
    ASSERT_FALSE(Config_ValidateValue("replication_target", ""));
    ASSERT_FALSE(Config_ValidateValue("dht_port", "12abc"));
    PASS();
}

TEST test_validate_boolean(void)
{
    ASSERT(Config_ValidateValue("dht_enabled", "0"));
    ASSERT(Config_ValidateValue("dht_enabled", "1"));
    ASSERT(Config_ValidateValue("ec_enabled", "0"));
    ASSERT(Config_ValidateValue("auto_evict_enabled", "1"));

    ASSERT_FALSE(Config_ValidateValue("dht_enabled", "2"));
    ASSERT_FALSE(Config_ValidateValue("ec_enabled", "yes"));
    ASSERT_FALSE(Config_ValidateValue("auto_evict_enabled", "true"));
    PASS();
}

TEST test_validate_string_passthrough(void)
{
    ASSERT(Config_ValidateValue("relay_host", "wormholerelay.com"));
    ASSERT(Config_ValidateValue("relay_host", ""));
    ASSERT(Config_ValidateValue("dht_bootstrap_nodes", "192.168.1.1:4568,10.0.0.1:4568"));
    PASS();
}

TEST test_set_rejects_invalid(void)
{
    char path[512];
    make_temp_path(path, sizeof(path), "validate_set.ini");

    WORMHOLE_CONFIG *config = Config_Load(path);
    ASSERT(config != NULL);

    // Out-of-range value should be rejected
    ASSERT_FALSE(Config_Set(config, "max_storage_gb", "0"));
    // Default should be unchanged
    ASSERT_EQ(Config_GetUint64(config, "max_storage_gb", 0),
              (uint64_t)CONFIG_DEFAULT_MAX_STORAGE_GB);

    // Valid value should succeed
    ASSERT(Config_Set(config, "max_storage_gb", "50"));
    ASSERT_EQ(Config_GetUint64(config, "max_storage_gb", 0), 50ULL);

    Config_Destroy(config);
    PASS();
}

TEST test_validate_unknown_key_allowed(void)
{
    // Unknown keys should pass validation (custom user keys)
    ASSERT(Config_ValidateValue("my_custom_key", "anything"));
    PASS();
}

SUITE(validation_suite)
{
    RUN_TEST(test_validate_numeric_in_range);
    RUN_TEST(test_validate_numeric_out_of_range);
    RUN_TEST(test_validate_numeric_not_a_number);
    RUN_TEST(test_validate_boolean);
    RUN_TEST(test_validate_string_passthrough);
    RUN_TEST(test_set_rejects_invalid);
    RUN_TEST(test_validate_unknown_key_allowed);
}

// ============================================================================
// Suites
// ============================================================================

SUITE(defaults_suite)
{
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_default_count);
}

SUITE(getset_suite)
{
    RUN_TEST(test_set_and_get_string);
    RUN_TEST(test_set_and_get_uint64);
    RUN_TEST(test_get_missing_key);
    RUN_TEST(test_get_uint64_invalid);
    RUN_TEST(test_overwrite_existing);
    RUN_TEST(test_case_insensitive);
    RUN_TEST(test_config_full);
}

SUITE(file_roundtrip_suite)
{
    RUN_TEST(test_save_and_reload);
    RUN_TEST(test_parse_comments);
    RUN_TEST(test_parse_whitespace);
    RUN_TEST(test_parse_empty_lines);
}

int main(void)
{
    setup_test_home();
    atexit(cleanup_test_home);

    GREATEST_MAIN_BEGIN();
    RUN_SUITE(defaults_suite);
    RUN_SUITE(getset_suite);
    RUN_SUITE(file_roundtrip_suite);
    RUN_SUITE(validation_suite);
    GREATEST_MAIN_END();
}
