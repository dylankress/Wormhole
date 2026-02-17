//
// config.c
// Wormhole - INI-style configuration file management.
// by Dylan Kress
//

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

//=============================================================================
// Internal helpers
//=============================================================================

// Trim leading and trailing whitespace in-place
static char *TrimWhitespace(char *str)
{
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Find an entry by key (case-insensitive)
static CONFIG_ENTRY *FindEntry(const WORMHOLE_CONFIG *config, const char *key)
{
    for (uint32_t i = 0; i < config->count; i++)
    {
#ifdef _WIN32
        if (_stricmp(config->entries[i].key, key) == 0)
#else
        if (strcasecmp(config->entries[i].key, key) == 0)
#endif
        {
            return (CONFIG_ENTRY *)&config->entries[i];
        }
    }
    return NULL;
}

// Set defaults for a new config
static void Config_SetDefaults(WORMHOLE_CONFIG *config)
{
    Config_SetUint64(config, "max_storage_gb", CONFIG_DEFAULT_MAX_STORAGE_GB);
    Config_SetUint64(config, "replication_target", CONFIG_DEFAULT_REPLICATION_TARGET);
    Config_Set(config, "relay_host", CONFIG_DEFAULT_RELAY_HOST);
    Config_SetUint64(config, "relay_port", CONFIG_DEFAULT_RELAY_PORT);
}

//=============================================================================
// Public API
//=============================================================================

BOOLEAN Config_GetDefaultPath(char *path, size_t path_len)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s\\.wormhole\\config", home);
#else
    const char *home = getenv("HOME");
    if (!home) return FALSE;
    snprintf(path, path_len, "%s/.wormhole/config", home);
#endif
    return TRUE;
}

WORMHOLE_CONFIG *Config_Load(const char *path)
{
    WORMHOLE_CONFIG *config = (WORMHOLE_CONFIG *)calloc(1, sizeof(WORMHOLE_CONFIG));
    if (!config) return NULL;

    if (path)
    {
        snprintf(config->filepath, sizeof(config->filepath), "%s", path);
    }

    // Set defaults first
    Config_SetDefaults(config);

    // Try to read file and override defaults
    FILE *fh = fopen(path, "r");
    if (!fh)
    {
        // File doesn't exist, return config with defaults
        LOG("[config] No config file at %s, using defaults\n", path);
        return config;
    }

    char line[CONFIG_MAX_KEY_LEN + CONFIG_MAX_VALUE_LEN + 16];
    while (fgets(line, sizeof(line), fh))
    {
        // Skip comments and empty lines
        char *trimmed = TrimWhitespace(line);
        if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0')
        {
            continue;
        }

        // Split on '='
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = TrimWhitespace(trimmed);
        char *value = TrimWhitespace(eq + 1);

        if (key[0] != '\0' && value[0] != '\0')
        {
            Config_Set(config, key, value);
        }
    }

    fclose(fh);
    LOG("[config] Loaded config from %s (%u entries)\n", path, config->count);
    return config;
}

WORMHOLE_CONFIG *Config_LoadDefault(void)
{
    char path[MAX_PATH];
    if (!Config_GetDefaultPath(path, sizeof(path)))
    {
        return NULL;
    }
    return Config_Load(path);
}

BOOLEAN Config_Save(const WORMHOLE_CONFIG *config)
{
    if (!config || config->filepath[0] == '\0')
    {
        LOG_ERROR("[config] No file path set\n");
        return FALSE;
    }

    FILE *fh = fopen(config->filepath, "w");
    if (!fh)
    {
        LOG_ERROR("[config] Failed to open %s for writing\n", config->filepath);
        return FALSE;
    }

    fprintf(fh, "# Wormhole configuration\n");
    fprintf(fh, "# Edit this file or use 'wormhole config set <key> <value>'\n\n");

    for (uint32_t i = 0; i < config->count; i++)
    {
        fprintf(fh, "%s = %s\n", config->entries[i].key, config->entries[i].value);
    }

    fclose(fh);
    LOG("[config] Saved config to %s\n", config->filepath);
    return TRUE;
}

void Config_Destroy(WORMHOLE_CONFIG *config)
{
    free(config);
}

uint64_t Config_GetUint64(const WORMHOLE_CONFIG *config, const char *key, uint64_t default_value)
{
    if (!config || !key) return default_value;

    const CONFIG_ENTRY *entry = FindEntry(config, key);
    if (!entry) return default_value;

    char *endptr = NULL;
    uint64_t val = strtoull(entry->value, &endptr, 10);
    if (endptr == entry->value)
    {
        return default_value;  // Not a valid number
    }

    return val;
}

const char *Config_GetString(const WORMHOLE_CONFIG *config, const char *key, const char *default_value)
{
    if (!config || !key) return default_value;

    const CONFIG_ENTRY *entry = FindEntry(config, key);
    if (!entry) return default_value;

    return entry->value;
}

BOOLEAN Config_Set(WORMHOLE_CONFIG *config, const char *key, const char *value)
{
    if (!config || !key || !value) return FALSE;

    // Check if key already exists
    CONFIG_ENTRY *entry = FindEntry(config, key);
    if (entry)
    {
        snprintf(entry->value, CONFIG_MAX_VALUE_LEN, "%s", value);
        return TRUE;
    }

    // Add new entry
    if (config->count >= CONFIG_MAX_ENTRIES)
    {
        LOG_ERROR("[config] Config full (%u entries)\n", config->count);
        return FALSE;
    }

    CONFIG_ENTRY *new_entry = &config->entries[config->count];
    snprintf(new_entry->key, CONFIG_MAX_KEY_LEN, "%s", key);
    snprintf(new_entry->value, CONFIG_MAX_VALUE_LEN, "%s", value);
    config->count++;

    return TRUE;
}

BOOLEAN Config_SetUint64(WORMHOLE_CONFIG *config, const char *key, uint64_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return Config_Set(config, key, buf);
}
