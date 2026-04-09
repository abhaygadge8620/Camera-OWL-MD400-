#include "ini_parser.h"
#include <ctype.h>
#include <stdlib.h>

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

int ini_set_value(const char *filename, const char *section, const char *key,
                  const char *value)
{
    FILE *file = NULL;
    char *content = NULL;
    long file_size = 0L;
    size_t read_len = 0U;
    char *out = NULL;
    size_t out_cap;
    size_t out_len = 0U;
    char *cursor;
    char *line;
    int in_target_section = 0;
    int section_found = 0;
    int key_written = 0;
    int rc = -1;
    const size_t extra_cap = 512U;
    const size_t section_len = strlen(section);
    const size_t key_len = strlen(key);

    if ((filename == NULL) || (section == NULL) || (key == NULL) || (value == NULL)) {
        return -1;
    }

    file = fopen(filename, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    file_size = ftell(file);
    if (file_size < 0L) {
        fclose(file);
        return -1;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    content = (char *)malloc((size_t)file_size + 1U);
    if (content == NULL) {
        fclose(file);
        return -1;
    }

    read_len = fread(content, 1U, (size_t)file_size, file);
    fclose(file);
    content[read_len] = '\0';

    out_cap = read_len + extra_cap + section_len + key_len + strlen(value);
    out = (char *)malloc(out_cap);
    if (out == NULL) {
        free(content);
        return -1;
    }
    out[0] = '\0';

    cursor = content;
    while (*cursor != '\0') {
        char *next = strchr(cursor, '\n');
        size_t line_len;
        char *trimmed;

        if (next != NULL) {
            line_len = (size_t)(next - cursor) + 1U;
            *next = '\0';
            line = cursor;
            cursor = next + 1;
        } else {
            line_len = strlen(cursor);
            line = cursor;
            cursor += line_len;
        }

        {
            char line_buf[MAX_LINE_LENGTH];
            size_t copy_len = line_len;
            char *eq;

            if ((copy_len > 0U) && (line[copy_len - 1U] == '\n')) {
                copy_len--;
            }
            if ((copy_len > 0U) && (line[copy_len - 1U] == '\r')) {
                copy_len--;
            }
            if (copy_len >= sizeof(line_buf)) {
                copy_len = sizeof(line_buf) - 1U;
            }
            (void)memcpy(line_buf, line, copy_len);
            line_buf[copy_len] = '\0';

            trimmed = trim_whitespace(line_buf);

            if (*trimmed == '[') {
                if ((in_target_section != 0) && (key_written == 0)) {
                    out_len += (size_t)snprintf(out + out_len, out_cap - out_len,
                                                "%s=%s\n", key, value);
                    key_written = 1;
                }

                in_target_section = 0;
                if ((strncmp(trimmed + 1, section, section_len) == 0) &&
                    (trimmed[section_len + 1U] == ']')) {
                    in_target_section = 1;
                    section_found = 1;
                }
            } else if (in_target_section != 0) {
                eq = strchr(trimmed, '=');
                if (eq != NULL) {
                    *eq = '\0';
                    if (strcmp(trim_whitespace(trimmed), key) == 0) {
                        out_len += (size_t)snprintf(out + out_len, out_cap - out_len,
                                                    "%s=%s\n", key, value);
                        key_written = 1;
                        continue;
                    }
                }
            }
        }

        if ((out_len + line_len + 1U) >= out_cap) {
            rc = -1;
            goto cleanup;
        }
        (void)memcpy(out + out_len, line, line_len);
        out_len += line_len;
        out[out_len] = '\0';

        if (next != NULL) {
            *next = '\n';
        }
    }

    if ((in_target_section != 0) && (key_written == 0)) {
        out_len += (size_t)snprintf(out + out_len, out_cap - out_len, "%s=%s\n", key, value);
        key_written = 1;
    }

    if (section_found == 0) {
        if ((out_len > 0U) && (out[out_len - 1U] != '\n')) {
            out[out_len++] = '\n';
        }
        out_len += (size_t)snprintf(out + out_len, out_cap - out_len,
                                    "[%s]\n%s=%s\n", section, key, value);
    }

    file = fopen(filename, "wb");
    if (file == NULL) {
        goto cleanup;
    }
    if (fwrite(out, 1U, out_len, file) != out_len) {
        fclose(file);
        goto cleanup;
    }
    fclose(file);
    rc = 0;

cleanup:
    free(out);
    free(content);
    return rc;
}
