//
// config.h
// Wormhole - Configuration management (INI-style ~/.wormhole/config).
// by Dylan Kress
//

#pragma once

#include "common.h"
#include <stdint.h>

// Maximum number of config entries
#define CONFIG_MAX_ENTRIES 64
#define CONFIG_MAX_KEY_LEN 64
#define CONFIG_MAX_VALUE_LEN 256

// Default configuration values
#define CONFIG_DEFAULT_MAX_STORAGE_GB     10
#define CONFIG_DEFAULT_REPLICATION_TARGET 4
#define CONFIG_DEFAULT_RELAY_HOST         "wormholerelay.com"
#define CONFIG_DEFAULT_RELAY_PORT         443

// DHT defaults
#define CONFIG_DEFAULT_DHT_PORT           4568
#define CONFIG_DEFAULT_DHT_ENABLED        1
#define CONFIG_DEFAULT_DHT_BOOTSTRAP      ""   // Comma-separated host:port list (empty = use relay)

// Erasure coding defaults
#define CONFIG_DEFAULT_EC_ENABLED         1
#define CONFIG_DEFAULT_EC_DATA_SHARDS     8
#define CONFIG_DEFAULT_EC_PARITY_SHARDS   4

// Auto-eviction defaults
#define CONFIG_DEFAULT_AUTO_EVICT         0    // Disabled by default — keep local copies

// Health and verification defaults
#define CONFIG_DEFAULT_MIN_STORAGE_RATIO  50   // 0.50 stored as integer percent
#define CONFIG_DEFAULT_HEALTH_CHECK_SEC   1800 // 30 minutes
#define CONFIG_DEFAULT_PROOF_CACHE_COUNT  8

// Config entry
typedef struct {
    char key[CONFIG_MAX_KEY_LEN];
    char value[CONFIG_MAX_VALUE_LEN];
} CONFIG_ENTRY;

// Config state
typedef struct {
    CONFIG_ENTRY entries[CONFIG_MAX_ENTRIES];
    uint32_t     count;
    char         filepath[MAX_PATH];
} WORMHOLE_CONFIG;

// Load config from file. Returns allocated config, or NULL on error.
// If the file doesn't exist, returns a config with defaults.
WORMHOLE_CONFIG *Config_Load(const char *path);

// Load from the default path (~/.wormhole/config).
WORMHOLE_CONFIG *Config_LoadDefault(void);

// Save config to its file path.
// Returns TRUE on success.
BOOLEAN Config_Save(const WORMHOLE_CONFIG *config);

// Free config memory.
void Config_Destroy(WORMHOLE_CONFIG *config);

// Get a uint64 value from config. Returns default_value if key not found.
uint64_t Config_GetUint64(const WORMHOLE_CONFIG *config, const char *key, uint64_t default_value);

// Get a string value from config. Returns default_value if key not found.
// The returned pointer is valid as long as the config is alive.
const char *Config_GetString(const WORMHOLE_CONFIG *config, const char *key, const char *default_value);

// Set a config value (string). Creates the key if it doesn't exist.
// Returns TRUE on success.
BOOLEAN Config_Set(WORMHOLE_CONFIG *config, const char *key, const char *value);

// Set a config value (uint64). Convenience wrapper.
BOOLEAN Config_SetUint64(WORMHOLE_CONFIG *config, const char *key, uint64_t value);

// Get the default config file path (~/.wormhole/config).
BOOLEAN Config_GetDefaultPath(char *path, size_t path_len);
