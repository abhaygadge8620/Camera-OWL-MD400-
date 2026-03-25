#define _CRT_SECURE_NO_WARNINGS

#include "config_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    SEC_NONE = 0,
    SEC_UART,
    SEC_INPUT_BUTTON_IDS,
    SEC_BUTTON_LED_IDS,
    SEC_INPUT_SWITCH_IDS,
    SEC_LED_IDS,
    SEC_KNOB_IDS,
    SEC_MODE_VALUES,
    SEC_FREQUENCY_VALUES
} ini_section_t;

typedef struct
{
    const char *name;
    int index;
} name_index_t;

static const name_index_t k_button_index[BUTTON_COUNT] =
{
    {"DRIVES_RESET", 0}, {"OPTICS_RESET", 1}, {"LRF_RESET", 2}, {"CCE_RESET", 3}, {"PARK", 4},
    {"ABORT", 5}, {"COMBAT", 6}, {"BCU", 7}, {"PREPAR", 8}, {"UNCAG", 9},
    {"DAY", 10}, {"LOW_LIGHT", 11}, {"THERMAL", 12}, {"DROP", 13}, {"FIRE", 14},
    {"LRF", 15}
};

static const name_index_t k_button_led_index[BUTTON_LED_COUNT] =
{
    {"DRIVES_RESET_LED", 0}, {"OPTICS_RESET_LED", 1}, {"LRF_RESET_LED", 2}, {"CCE_RESET_LED", 3}, {"PARK_LED", 4},
    {"ABORT_LED", 5}, {"COMBAT_LED", 6}, {"BCU_LED", 7}, {"PREPAR_LED", 8}, {"UNCAG_LED", 9},
    {"DAY_LED", 10}, {"LOW_LIGHT_LED", 11}, {"THERMAL_LED", 12}, {"DROP_LED", 13}, {"FIRE_LED", 14},
    {"LRF_LED", 15}, {"SW_LRF_LED", 16}
};

static const name_index_t k_switch_index[SWITCH_COUNT] =
{
    {"MAIN_POWER_ON", 0}, {"DRIVES_ON", 1}, {"OPTICS_ON", 2}, {"LRF_ON", 3}, {"CCE_ON", 4},
    {"LOCK", 5}, {"STRELETS_CH1_ON", 6}, {"STRELETS_CH2_ON", 7}, {"SINGLE", 8}, {"CTRL_SRC_WCS", 9},
    {"HEAT_FILTER_ON", 10}, {"LAUNCH_KEY_EN", 11}, {"TARGET_SPEED_FAST", 12}, {"CHASE_HEAD", 13},
    {"CHASE_TAIL", 14}, {"TARGET_SPEED_SLOW", 15}, {"CTRL_SRC_CCE", 16}, {"SALVO", 17}
};

static const name_index_t k_led_index[LED_COUNT] =
{
    {"EXT_GYRO", 0}, {"EXT_WIND", 1}, {"EXT_LOG", 2}, {"EXT_BLOCK", 3}, {"TRN_LOCK", 4},
    {"TRN_UNLOCK", 5}, {"TRN_MOVE", 6}, {"ELV_LOCK", 7}, {"ELV_UNLOCK", 8}, {"ELV_MOVE", 9},
    {"MSL_ACTIVE", 10}, {"MSL_READY", 11}, {"MSL_LOCK", 12}, {"MSL_LAUNCH", 13},
    {"PWR_AVAILABLE", 14}, {"PWR_STATUS", 15}, {"LAUNCH_DISABLED", 16}, {"LAUNCH_ENABLED", 17},
    {"PORT1_MSL_2", 18}, {"PORT1_MSL_4", 19}, {"STBD1_MSL_1", 20}, {"STBD1_MSL_3", 21}
};

static const char *g_button_name_by_id[256];
static const char *g_button_led_name_by_id[256];
static const char *g_switch_name_by_id[256];
static const char *g_led_name_by_id[256];
static const char *g_knob_name_by_id[256];

static void trim_inplace(char *s)
{
    char *p = s;
    size_t n;

    while ((*p != '\0') && isspace((unsigned char)*p))
    {
        p++;
    }
    if (p != s)
    {
        memmove(s, p, strlen(p) + 1U);
    }

    n = strlen(s);
    while ((n > 0U) && isspace((unsigned char)s[n - 1U]))
    {
        s[n - 1U] = '\0';
        n--;
    }
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *endp = NULL;
    unsigned long v;
    if ((s == NULL) || (out == NULL))
    {
        return -1;
    }
    v = strtoul(s, &endp, 0);
    if ((endp == s) || (*endp != '\0'))
    {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int parse_u8(const char *s, uint8_t *out)
{
    uint32_t v = 0U;
    if ((parse_u32(s, &v) != 0) || (v > 255U))
    {
        return -1;
    }
    *out = (uint8_t)v;
    return 0;
}

static int parse_s8_as_u8(const char *s, uint8_t *out)
{
    char *endp = NULL;
    long v;
    if ((s == NULL) || (out == NULL))
    {
        return -1;
    }
    v = strtol(s, &endp, 0);
    if ((endp == s) || (*endp != '\0') || (v < -128L) || (v > 127L))
    {
        return -1;
    }
    *out = (uint8_t)((int8_t)v);
    return 0;
}

static int find_index(const char *name, const name_index_t *table, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        if (strcmp(name, table[i].name) == 0)
        {
            return table[i].index;
        }
    }
    return -1;
}

static void set_id_entry(id_entry_t *entry, const char *name, uint8_t id);

static int set_next_free_entry(id_entry_t *entries, int count, const char *name, uint8_t id)
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (entries[i].valid == 0U)
        {
            set_id_entry(&entries[i], name, id);
            return 0;
        }
    }
    return -1;
}

static void config_set_defaults(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->uart_device, "COM6", sizeof(cfg->uart_device) - 1U);
    cfg->uart_baud = 115200U;
    cfg->uart_read_timeout_ms = 0U;
}

static void reset_name_maps(void)
{
    memset(g_button_name_by_id, 0, sizeof(g_button_name_by_id));
    memset(g_button_led_name_by_id, 0, sizeof(g_button_led_name_by_id));
    memset(g_switch_name_by_id, 0, sizeof(g_switch_name_by_id));
    memset(g_led_name_by_id, 0, sizeof(g_led_name_by_id));
    memset(g_knob_name_by_id, 0, sizeof(g_knob_name_by_id));
}

static void build_name_maps(const Config *cfg)
{
    int i;
    reset_name_maps();

    for (i = 0; i < BUTTON_COUNT; i++)
    {
        if (cfg->button_ids[i].valid != 0U)
        {
            g_button_name_by_id[cfg->button_ids[i].id] = cfg->button_ids[i].name;
        }
    }
    for (i = 0; i < BUTTON_LED_COUNT; i++)
    {
        if (cfg->button_led_ids[i].valid != 0U)
        {
            g_button_led_name_by_id[cfg->button_led_ids[i].id] = cfg->button_led_ids[i].name;
        }
    }
    for (i = 0; i < SWITCH_COUNT; i++)
    {
        if (cfg->switch_ids[i].valid != 0U)
        {
            g_switch_name_by_id[cfg->switch_ids[i].id] = cfg->switch_ids[i].name;
        }
    }
    for (i = 0; i < LED_COUNT; i++)
    {
        if (cfg->led_ids[i].valid != 0U)
        {
            g_led_name_by_id[cfg->led_ids[i].id] = cfg->led_ids[i].name;
        }
    }
    for (i = 0; i < KNOB_COUNT; i++)
    {
        if (cfg->knob_ids[i].valid != 0U)
        {
            g_knob_name_by_id[cfg->knob_ids[i].id] = cfg->knob_ids[i].name;
        }
    }
}

static void set_id_entry(id_entry_t *entry, const char *name, uint8_t id)
{
    size_t n;
    memset(entry, 0, sizeof(*entry));
    n = strlen(name);
    if (n >= sizeof(entry->name))
    {
        n = sizeof(entry->name) - 1U;
    }
    memcpy(entry->name, name, n);
    entry->name[n] = '\0';
    entry->id = id;
    entry->valid = 1U;
}

int config_load(const char *path, Config *cfg)
{
    FILE *fp;
    char line[256];
    ini_section_t section = SEC_NONE;

    if ((path == NULL) || (cfg == NULL))
    {
        return -1;
    }

    config_set_defaults(cfg);
    reset_name_maps();

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return -2;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *eq;
        char key[128];
        char val[128];
        uint8_t id = 0U;
        int idx = -1;

        line[strcspn(line, "\r\n")] = '\0';
        trim_inplace(line);
        if ((line[0] == '\0') || (line[0] == '#') || (line[0] == ';'))
        {
            continue;
        }

        if ((line[0] == '[') && (line[strlen(line) - 1U] == ']'))
        {
            line[strlen(line) - 1U] = '\0';
            memmove(line, line + 1, strlen(line));
            trim_inplace(line);

            if (strcmp(line, "UART") == 0) section = SEC_UART;
            else if (strcmp(line, "INPUT_BUTTON_IDS") == 0) section = SEC_INPUT_BUTTON_IDS;
            else if (strcmp(line, "BUTTON_LED_IDS") == 0) section = SEC_BUTTON_LED_IDS;
            else if (strcmp(line, "INPUT_SWITCH_IDS") == 0) section = SEC_INPUT_SWITCH_IDS;
            else if (strcmp(line, "LED_IDS") == 0) section = SEC_LED_IDS;
            else if (strcmp(line, "KNOB_IDS") == 0) section = SEC_KNOB_IDS;
            else if (strcmp(line, "MODE_VALUES") == 0) section = SEC_MODE_VALUES;
            else if (strcmp(line, "FREQUENCY_VALUES") == 0) section = SEC_FREQUENCY_VALUES;
            else section = SEC_NONE;
            continue;
        }

        eq = strchr(line, '=');
        if (eq == NULL)
        {
            continue;
        }

        *eq = '\0';
        strncpy(key, line, sizeof(key) - 1U);
        key[sizeof(key) - 1U] = '\0';
        strncpy(val, eq + 1, sizeof(val) - 1U);
        val[sizeof(val) - 1U] = '\0';
        trim_inplace(key);
        trim_inplace(val);

        if (section == SEC_UART)
        {
            if (strcmp(key, "device") == 0)
            {
                strncpy(cfg->uart_device, val, sizeof(cfg->uart_device) - 1U);
                cfg->uart_device[sizeof(cfg->uart_device) - 1U] = '\0';
            }
            else if (strcmp(key, "baud") == 0)
            {
                (void)parse_u32(val, &cfg->uart_baud);
            }
            else if (strcmp(key, "read_timeout_ms") == 0)
            {
                (void)parse_u32(val, &cfg->uart_read_timeout_ms);
            }
            continue;
        }

        if (section == SEC_MODE_VALUES)
        {
            if (parse_s8_as_u8(val, &id) != 0)
            {
                continue;
            }
        }
        else
        {
            if (parse_u8(val, &id) != 0)
            {
                continue;
            }
        }

        if (section == SEC_INPUT_BUTTON_IDS)
        {
            idx = find_index(key, k_button_index, BUTTON_COUNT);
            if (idx >= 0) set_id_entry(&cfg->button_ids[idx], key, id);
        }
        else if (section == SEC_BUTTON_LED_IDS)
        {
            idx = find_index(key, k_button_led_index, BUTTON_LED_COUNT);
            if (idx >= 0) set_id_entry(&cfg->button_led_ids[idx], key, id);
        }
        else if (section == SEC_INPUT_SWITCH_IDS)
        {
            idx = find_index(key, k_switch_index, SWITCH_COUNT);
            if (idx >= 0) set_id_entry(&cfg->switch_ids[idx], key, id);
        }
        else if (section == SEC_LED_IDS)
        {
            idx = find_index(key, k_led_index, LED_COUNT);
            if (idx >= 0) set_id_entry(&cfg->led_ids[idx], key, id);
        }
        else if (section == SEC_KNOB_IDS)
        {
            (void)set_next_free_entry(cfg->knob_ids, KNOB_COUNT, key, id);
        }
        else if (section == SEC_MODE_VALUES)
        {
            (void)set_next_free_entry(cfg->mode_values, MODE_VALUE_COUNT, key, id);
        }
        else if (section == SEC_FREQUENCY_VALUES)
        {
            (void)set_next_free_entry(cfg->frequency_values, FREQUENCY_VALUE_COUNT, key, id);
        }
    }

    fclose(fp);
    build_name_maps(cfg);
    return 0;
}

const char *get_button_name_by_id(uint8_t id)
{
    return g_button_name_by_id[id];
}

const char *get_switch_name_by_id(uint8_t id)
{
    return g_switch_name_by_id[id];
}

const char *get_button_led_name_by_id(uint8_t id)
{
    return g_button_led_name_by_id[id];
}

const char *get_led_name_by_id(uint8_t id)
{
    return g_led_name_by_id[id];
}

const char *get_knob_name_by_id(uint8_t id)
{
    return g_knob_name_by_id[id];
}

int config_get_led_id(const Config *cfg, const char *name, uint8_t *id_out)
{
    int i;
    if ((cfg == NULL) || (name == NULL) || (id_out == NULL))
    {
        return -1;
    }
    for (i = 0; i < LED_COUNT; i++)
    {
        if ((cfg->led_ids[i].valid != 0U) && (strcmp(cfg->led_ids[i].name, name) == 0))
        {
            *id_out = cfg->led_ids[i].id;
            return 0;
        }
    }
    return -2;
}

int config_get_button_led_id(const Config *cfg, const char *name, uint8_t *id_out)
{
    int i;
    if ((cfg == NULL) || (name == NULL) || (id_out == NULL))
    {
        return -1;
    }
    for (i = 0; i < BUTTON_LED_COUNT; i++)
    {
        if ((cfg->button_led_ids[i].valid != 0U) && (strcmp(cfg->button_led_ids[i].name, name) == 0))
        {
            *id_out = cfg->button_led_ids[i].id;
            return 0;
        }
    }
    return -2;
}

int config_get_button_led_id_by_button_name(const Config *cfg, const char *button_name, uint8_t *id_out)
{
    char mapped_name[40];
    int n;

    if ((cfg == NULL) || (button_name == NULL) || (id_out == NULL))
    {
        return -1;
    }

    n = snprintf(mapped_name, sizeof(mapped_name), "%s_LED", button_name);
    if ((n < 0) || ((size_t)n >= sizeof(mapped_name)))
    {
        return -3;
    }

    return config_get_button_led_id(cfg, mapped_name, id_out);
}
