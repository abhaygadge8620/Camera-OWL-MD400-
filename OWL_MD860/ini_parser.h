#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <stdio.h>
#include <string.h>

#define MAX_LINE_LENGTH 256
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 128

/**
 * @file ini_parser.h
 * @brief Simple INI file parser header.
 *
 * @details This header defines a basic API to parse key-value pairs from an
 * INI-style configuration file.
 */

/**
 * @brief Finds a key in a given section and stores its value.
 * @param filename The path to the INI file.
 * @param section The name of the section (e.g., "imu_interface").
 * @param key The name of the key to find (e.g., "mode").
 * @param value_out The buffer to store the value.
 * @param max_len The maximum length of the value buffer.
 * @return 0 on success, -1 if the key or section is not found.
 */
int ini_get_value(const char *filename, const char *section, const char *key,
                  char *value_out, size_t max_len);

#endif // INI_PARSER_H