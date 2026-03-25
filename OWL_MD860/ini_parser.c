#include "ini_parser.h"
#include <ctype.h>

/**
 * @file ini_parser.c
 * @brief Simple INI file parser implementation.
 *
 * @details Implements a basic parser to read key-value pairs from an
 * INI-style configuration file.
 */

/**
 * @brief Trims leading and trailing whitespace from a string.
 * @param str The string to trim.
 * @return A pointer to the trimmed string.
 */
static char* trim_whitespace(char *s) {
    if (!s) return s;

    // Trim leading
    while (*s && isspace((unsigned char)*s)) s++;

    // If all spaces
    if (*s == '\0') return s;

    // Trim trailing
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        --end;
    }

    return s;
}

// Remove inline comments starting with ';' or '#' (outside of quotes), in-place.
static void strip_inline_comment(char *s) {
    int in_quote = 0;
    for (char *p = s; *p; ++p) {
        if (*p == '"') in_quote = !in_quote;
        if (!in_quote && (*p == ';' || *p == '#')) { *p = '\0'; break; }
    }
}

/* Find [section] and key=value. Copies trimmed value into value_out.
 * Returns 0 on success, -1 on failure (as per header).
 */
int ini_get_value(const char *filename, const char *section, const char *key,
                  char *value_out, size_t max_len)
{
    if (!filename || !section || !key || !value_out || max_len == 0) return -1;

    FILE *file = fopen(filename, "r");
    if (!file) return -1;

    char line[MAX_LINE_LENGTH];
    char current_section[MAX_KEY_LENGTH] = "";

    while (fgets(line, sizeof(line), file)) {
        // Normalize line endings and whitespace
        // (fgets keeps '\n'; trim_whitespace handles both '\n' and '\r')
        char *ln = trim_whitespace(line);
        if (*ln == '\0') continue;                 // empty
        if (*ln == ';' || *ln == '#') continue;    // full-line comment

        // Section header?
        if (*ln == '[') {
            char *rb = strchr(ln, ']');
            if (!rb) continue; // malformed section line; skip
            *rb = '\0';
            char *sec_name = trim_whitespace(ln + 1);
            strncpy(current_section, sec_name, sizeof(current_section));
            current_section[sizeof(current_section)-1] = '\0';
            continue;
        }

        // Only parse keys inside the requested section
        if (strcmp(current_section, section) != 0) continue;

        // key=value ?
        char *eq = strchr(ln, '=');
        if (!eq) continue;

        // Split into key / value substrings
        *eq = '\0';
        char *key_part = trim_whitespace(ln);
        char *val_part = trim_whitespace(eq + 1);

        // Remove inline comments on the value
        strip_inline_comment(val_part);
        val_part = trim_whitespace(val_part);

        if (strcmp(key_part, key) == 0) {
            // Copy out value safely
            strncpy(value_out, val_part, max_len);
            value_out[max_len - 1] = '\0';
            fclose(file);
            return 0; // success
        }
    }

    fclose(file);
    return -1; // not found
}
