#ifndef MQTT_H
#define MQTT_H

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>


/**
 * @file mqtt.h
 * @brief Lightweight wrapper around Paho MQTT for publishing data and logs.
 */

/* Optional: override these via -D flags or change defaults here */
#ifndef MQTT_INFO_TOPIC
#define MQTT_INFO_TOPIC     "log/info"
#endif
#ifndef MQTT_WARN_TOPIC
#define MQTT_WARN_TOPIC     "log/warn"
#endif
#ifndef MQTT_ERROR_TOPIC
#define MQTT_ERROR_TOPIC    "log/error"
#endif
#ifndef MQTT_EXCEPT_TOPIC
#define MQTT_EXCEPT_TOPIC   "log/exception"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and connect to MQTT broker.
 * @param broker_addr e.g. "tcp://127.0.0.1:1883"
 * @param client_id   unique client id
 * @return 0 on success, -1 on failure
 */
int mqtt_init(const char* broker_addr, const char* client_id);

int mqtt_is_connected(void);



/**
 * @brief Publish a binary payload to a topic (QoS 1).
 * @return 0 on success, -1 on failure
 */
int mqtt_publish(const char *topic, const void *payload, size_t payload_len);

/* Subscribe to a topic (QoS 1). Returns 0 on success, -1 on failure. */
int mqtt_subscribe(const char *topic);

/* User callback type for inbound messages (payload is ASCII; length provided). */
typedef void (*mqtt_rx_fn_t)(const char *topic, const char *payload, size_t len);

/**
 * @brief Register an inbound message handler (optional). Safe to call anytime.
 */
void mqtt_set_rx_handler(mqtt_rx_fn_t fn);

/**
 * @brief Publish a formatted log string to a topic (QoS 0).
 */
void mqtt_log_publish(const char* topic, const char* fmt, ...);

/**
 * @brief Disconnect and cleanup.
 */
void mqtt_close(void);

/* -------- Convenience logging macros (also print to stdout/stderr) -------- */
// Enable/disable MQTT logging (this can be defined/undefined as needed, e.g., via compiler flags)
// #define ENABLE_MQTT_LOGGING

// Helper macro to conditionally include MQTT publishing
#ifdef ENABLE_MQTT_LOGGING
#define LOG_PUBLISH(topic, format, ...) mqtt_log_publish(topic, format, ##__VA_ARGS__)
#else
#define LOG_PUBLISH(topic, format, ...)
#endif

#define log_info(format, ...) \
    do { \
        fprintf(stdout, "[INFO] " format "\n", ##__VA_ARGS__); \
        LOG_PUBLISH(MQTT_INFO_TOPIC, format, ##__VA_ARGS__); \
    } while (0)

#define log_warning(format, ...) \
    do { \
        fprintf(stdout, "[WARN] " format "\n", ##__VA_ARGS__); \
        LOG_PUBLISH(MQTT_WARN_TOPIC, format, ##__VA_ARGS__); \
    } while (0)

#define log_error(format, ...) \
    do { \
        fprintf(stderr, "[ERROR] " format "\n", ##__VA_ARGS__); \
        LOG_PUBLISH(MQTT_ERROR_TOPIC, format, ##__VA_ARGS__); \
    } while (0)

#define log_exception(format, ...) \
    do { \
        fprintf(stderr, "[EXCEPTION] " format "\n", ##__VA_ARGS__); \
        LOG_PUBLISH(MQTT_EXCEPT_TOPIC, format, ##__VA_ARGS__); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* MQTT_H */
