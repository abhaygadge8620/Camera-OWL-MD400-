#include "ini_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *uart_ini_trim_whitespace(char *s)
{
    char *end;

    if (s == NULL) {
        return NULL;
    }

    while ((*s != '\0') && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }

    end = s + strlen(s) - 1;
    while ((end > s) && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }

    return s;
}

static void uart_ini_strip_inline_comment(char *s)
{
    int in_quote = 0;
    char *p;

    if (s == NULL) {
        return;
    }

    for (p = s; *p != '\0'; ++p) {
        if (*p == '"') {
            in_quote = (in_quote == 0) ? 1 : 0;
        }
        if ((in_quote == 0) && ((*p == ';') || (*p == '#'))) {
            *p = '\0';
            break;
        }
    }
}

int ini_get_value(const char *filename, const char *section, const char *key,
                  char *value_out, size_t max_len)
{
    FILE *file;
    char line[UART_INI_MAX_LINE_LENGTH];
    char current_section[UART_INI_MAX_KEY_LENGTH] = "";

    if ((filename == NULL) || (section == NULL) || (key == NULL) ||
        (value_out == NULL) || (max_len == 0u)) {
        return -1;
    }

    file = fopen(filename, "r");
    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *ln = uart_ini_trim_whitespace(line);

        if ((ln == NULL) || (*ln == '\0') || (*ln == ';') || (*ln == '#')) {
            continue;
        }

        if (*ln == '[') {
            char *rb = strchr(ln, ']');
            if (rb == NULL) {
                continue;
            }
            *rb = '\0';
            ln = uart_ini_trim_whitespace(ln + 1);
            if (ln == NULL) {
                continue;
            }
            (void)strncpy(current_section, ln, sizeof(current_section) - 1u);
            current_section[sizeof(current_section) - 1u] = '\0';
            continue;
        }

        if (strcmp(current_section, section) == 0) {
            char *eq = strchr(ln, '=');
            if (eq != NULL) {
                char *key_part;
                char *val_part;

                *eq = '\0';
                key_part = uart_ini_trim_whitespace(ln);
                val_part = uart_ini_trim_whitespace(eq + 1);
                uart_ini_strip_inline_comment(val_part);
                val_part = uart_ini_trim_whitespace(val_part);

                if ((key_part != NULL) && (val_part != NULL) && (strcmp(key_part, key) == 0)) {
                    (void)strncpy(value_out, val_part, max_len - 1u);
                    value_out[max_len - 1u] = '\0';
                    (void)fclose(file);
                    return 0;
                }
            }
        }
    }

    (void)fclose(file);
    return -1;
}
