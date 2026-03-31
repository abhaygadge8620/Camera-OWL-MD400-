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
    const char *canonical_name;
    int index;
} name_index_t;

static const name_index_t k_button_index[BUTTON_COUNT] =
{
    {"DRIVES_RESET", "DRIVES_RESET", 0}, {"OPTICS_RESET", "OPTICS_RESET", 1},
    {"LRF_RESET", "LRF_RESET", 2}, {"CCE_RESET", "CCE_RESET", 3}, {"PARK", "PARK", 4},
    {"ABORT", "ABORT", 5}, {"COMBAT", "COMBAT", 6}, {"BCU", "BCU", 7},
    {"PREPAR", "PREPAR", 8}, {"UNCAG", "UNCAG", 9}, {"DAY", "DAY", 10},
    {"LOW_LIGHT", "LOW_LIGHT", 11}, {"THERMAL", "THERMAL", 12}, {"DROP", "DROP", 13},
    {"FIRE", "FIRE", 14}, {"LRF", "LRF", 15}, {"GButton1", "GButton1", 16},
    {"GButton2", "GButton2", 17}, {"GButton3", "GButton3", 18}, {"GButton4", "GButton4", 19},
    {"GButton5", "GButton5", 20}, {"GButton6", "GButton6", 21}
};

static const name_index_t k_button_led_index[] =
{
    {"DRIVES_RESET_LED", "DRIVES_RESET_LED", 0}, {"OPTICS_RESET_LED", "OPTICS_RESET_LED", 1},
    {"LRF_RESET_LED", "LRF_RESET_LED", 2}, {"CCE_RESET_LED", "CCE_RESET_LED", 3},
    {"PARK_LED", "PARK_LED", 4}, {"ABORT_LED", "ABORT_LED", 5}, {"COMBAT_LED", "COMBAT_LED", 6},
    {"BCU_LED", "BCU_LED", 7}, {"PREPAR_LED", "PREPAR_LED", 8}, {"UNCAG_LED", "UNCAG_LED", 9},
    {"DAY_LED", "DAY_LED", 10}, {"LOW_LIGHT_LED", "LOW_LIGHT_LED", 11},
    {"THERMAL_LED", "THERMAL_LED", 12}, {"DROP_LED", "DROP_LED", 13}, {"FIRE_LED", "FIRE_LED", 14},
    {"LRF_LED", "LRF_LED", 15}, {"SW_LRF_LED", "SW_LRF_LED", 16},
    {"SW_LRF_Status_LED", "SW_LRF_LED", 16}, {"GBUTTON1_LED", "GBUTTON1_LED", 17},
    {"GBUTTON2_LED", "GBUTTON2_LED", 18}, {"GBUTTON3_LED", "GBUTTON3_LED", 19},
    {"GBUTTON4_LED", "GBUTTON4_LED", 20}, {"GBUTTON5_LED", "GBUTTON5_LED", 21},
    {"GBUTTON6_LED", "GBUTTON6_LED", 22}, {"SW_UnLOCK_Status_LED", "SW_UnLOCK_Status_LED", 23},
    {"SW_UNLOCK_Status_LED", "SW_UnLOCK_Status_LED", 23},
    {"SW_STRELETS_CH1_Status_LED", "SW_STRELETS_CH1_Status_LED", 24},
    {"SW_STRELETS_CH2_Status_LED", "SW_STRELETS_CH2_Status_LED", 25},
    {"SW_CCE_Status_LED", "SW_CCE_Status_LED", 26},
    {"SW_Optics_Status_LED", "SW_Optics_Status_LED", 27},
    {"SW_Drives_Status_LED", "SW_Drives_Status_LED", 28}
};

static const name_index_t k_switch_index[] =
{
    {"MAIN_POWER_ON", "MAIN_POWER_ON", 0}, {"DRIVES_ON", "DRIVES_ON", 1},
    {"OPTICS_ON", "OPTICS_ON", 2}, {"LRF_ON", "LRF_ON", 3}, {"CCE_ON", "CCE_ON", 4},
    {"LOCK", "LOCK", 5}, {"STRELETS_CH1_ON", "STRELETS_CH1_ON", 6},
    {"STRELETS_CH2_ON", "STRELETS_CH2_ON", 7}, {"SINGLE", "SINGLE", 8},
    {"SALVO_SINGLE", "SINGLE", 8}, {"CTRL_SRC_WCS", "CTRL_SRC_WCS", 9},
    {"HEAT_FILTER_ON", "HEAT_FILTER_ON", 10}, {"LAUNCH_KEY_EN", "LAUNCH_KEY_EN", 11},
    {"TARGET_SPEED_FAST", "TARGET_SPEED_FAST", 12}, {"CHASE_HEAD", "CHASE_HEAD", 13},
    {"CHASE_TAIL", "CHASE_TAIL", 14}, {"TARGET_SPEED_SLOW", "TARGET_SPEED_SLOW", 15},
    {"CTRL_SRC_CCE", "CTRL_SRC_CCE", 16}, {"SALVO", "SALVO", 17}, {"UNLOCK", "UNLOCK", 18}
};

static const name_index_t k_led_index[LED_COUNT] =
{
    {"EXT_GYRO", "EXT_GYRO", 0}, {"EXT_WIND", "EXT_WIND", 1}, {"EXT_LOG", "EXT_LOG", 2},
    {"EXT_BLOCK", "EXT_BLOCK", 3}, {"TRN_LOCK", "TRN_LOCK", 4},
    {"TRN_UNLOCK", "TRN_UNLOCK", 5}, {"TRN_MOVE", "TRN_MOVE", 6},
    {"ELV_LOCK", "ELV_LOCK", 7}, {"ELV_UNLOCK", "ELV_UNLOCK", 8},
    {"ELV_MOVE", "ELV_MOVE", 9}, {"MSL_ACTIVE", "MSL_ACTIVE", 10},
    {"MSL_READY", "MSL_READY", 11}, {"MSL_LOCK", "MSL_LOCK", 12},
    {"MSL_LAUNCH", "MSL_LAUNCH", 13}, {"PWR_AVAILABLE", "PWR_AVAILABLE", 14},
    {"PWR_STATUS", "PWR_STATUS", 15}, {"LAUNCH_DISABLED", "LAUNCH_DISABLED", 16},
    {"LAUNCH_ENABLED", "LAUNCH_ENABLED", 17}, {"PORT1_MSL_2", "PORT1_MSL_2", 18},
    {"PORT1_MSL_4", "PORT1_MSL_4", 19}, {"STBD1_MSL_1", "STBD1_MSL_1", 20},
    {"STBD1_MSL_3", "STBD1_MSL_3", 21}
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

static const name_index_t *find_name_entry(const char *name, const name_index_t *table, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        if (strcmp(name, table[i].name) == 0)
        {
            return &table[i];
        }
    }
    return NULL;
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
    strncpy(cfg->uart_device, "/dev/ttyUSB0", sizeof(cfg->uart_device) - 1U);
    cfg->uart_baud = 115200U;
    cfg->uart_read_timeout_ms = 20U;
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
        const name_index_t *entry = NULL;

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
            entry = find_name_entry(key, k_button_index, BUTTON_COUNT);
            if (entry != NULL) set_id_entry(&cfg->button_ids[entry->index], entry->canonical_name, id);
        }
        else if (section == SEC_BUTTON_LED_IDS)
        {
            entry = find_name_entry(key, k_button_led_index, (int)(sizeof(k_button_led_index) / sizeof(k_button_led_index[0])));
            if (entry != NULL) set_id_entry(&cfg->button_led_ids[entry->index], entry->canonical_name, id);
        }
        else if (section == SEC_INPUT_SWITCH_IDS)
        {
            entry = find_name_entry(key, k_switch_index, (int)(sizeof(k_switch_index) / sizeof(k_switch_index[0])));
            if (entry != NULL) set_id_entry(&cfg->switch_ids[entry->index], entry->canonical_name, id);
        }
        else if (section == SEC_LED_IDS)
        {
            entry = find_name_entry(key, k_led_index, LED_COUNT);
            if (entry != NULL) set_id_entry(&cfg->led_ids[entry->index], entry->canonical_name, id);
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
