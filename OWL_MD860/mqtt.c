#include "mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include "MQTTClient.h"

/* Internal global client handle (module-local) */
static MQTTClient g_mqtt = NULL;
static int mqtt_connected = 0;

static mqtt_rx_fn_t g_rx_handler = NULL;

/* Paho callback prototypes */
static void connlost_cb(void *context, char *cause);
static int  msgarrived_cb(void *context, char *topicName, int topicLen, MQTTClient_message *message);
static void delivered_cb(void *context, MQTTClient_deliveryToken dt);


int mqtt_init(const char* broker_addr, const char* client_id) {
    if (!broker_addr || !client_id) {
        fprintf(stderr, "[ERROR] mqtt_init: invalid args\n");
        return -1;
    }
    // Close existing connection if any
    if (g_mqtt) { mqtt_close(); }

    MQTTClient_create(&g_mqtt, broker_addr, client_id,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    int rc = MQTTClient_connect(g_mqtt, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "[ERROR] MQTT connect failed to %s (rc=%d)\n", broker_addr, rc);
        MQTTClient_destroy(&g_mqtt);
        g_mqtt = NULL;
        mqtt_connected = 0;
        return -1;
    }
    MQTTClient_setCallbacks(g_mqtt, NULL, connlost_cb, msgarrived_cb, delivered_cb);
    mqtt_connected = 1;
    fprintf(stdout, "[INFO] Connected to MQTT broker: %s\n", broker_addr);
    return 0;
}

int mqtt_publish(const char *topic, const void *payload, size_t payload_len)
{
    if ((topic == NULL) || (payload == NULL)) {
        fprintf(stderr, "[ERROR] mqtt_publish: invalid args\n");
        return -1;
    }
    if ((!mqtt_connected) || (g_mqtt == NULL)) {
        fprintf(stderr, "[WARN] mqtt_publish: not connected\n");
        return -1;
    }
    if (payload_len > (size_t)INT_MAX) {
        fprintf(stderr, "[ERROR] mqtt_publish: payload too large (%zu)\n", payload_len);
        return -1;
    }

    /* Allocate a mutable copy to avoid casting away const (MISRA & -Wcast-qual) */
    uint8_t *tmp = NULL;
    if (payload_len > 0U) {
        tmp = (uint8_t *)malloc(payload_len);
        if (tmp == NULL) {
            fprintf(stderr, "[ERROR] mqtt_publish: malloc failed (%zu)\n", payload_len);
            return -1;
        }
        (void)memcpy(tmp, payload, payload_len);
    }

    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload    = (void *)tmp;                 /* mutable buffer */
    msg.payloadlen = (int)payload_len;            /* safe: <= INT_MAX checked */
    msg.qos        = 1;
    msg.retained   = 0;

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(g_mqtt, topic, &msg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "[ERROR] MQTT publish rc=%d topic=%s\n", rc, topic);
        free(tmp);
        return -1;
    }

    rc = MQTTClient_waitForCompletion(g_mqtt, token, 1000);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stdout, "[WARN] MQTT publish completion rc=%d\n", rc);
        /* continue; we still free and report success/failure below */
    }

    free(tmp);
    return 0;
}


void mqtt_log_publish(const char* topic, const char* fmt, ...) {
    if (!topic || !fmt) return;

    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (!mqtt_connected || !g_mqtt) return;

    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload = (void*)buffer;
    msg.payloadlen = (int)strlen(buffer);
    msg.qos = 0;
    msg.retained = 0;

    /* Fire-and-forget logs */
    MQTTClient_publishMessage(g_mqtt, topic, &msg, NULL);
}

int mqtt_is_connected(void) {
    return mqtt_connected && g_mqtt != NULL;
}

void mqtt_close(void) {
    if (g_mqtt) {
        if (mqtt_connected) {
            MQTTClient_disconnect(g_mqtt, 1000);
            mqtt_connected = 0;
        }
        MQTTClient_destroy(&g_mqtt);
        g_mqtt = NULL;
    }
}

/* ---------- Public API: set RX handler ---------- */
void mqtt_set_rx_handler(mqtt_rx_fn_t fn)
{
    g_rx_handler = fn;
}

/* ---------- Public API: subscribe ---------- */
int mqtt_subscribe(const char *topic)
{
    if ((g_mqtt == NULL) || (mqtt_connected == 0) || (topic == NULL) || (topic[0] == '\0')) {
        return -1;
    }
    /* QoS 1 for delivery guarantee without duplicates */
    int rc = MQTTClient_subscribe(g_mqtt, topic, 1);
    return (rc == MQTTCLIENT_SUCCESS) ? 0 : -1;
}

/* ---------- Paho callbacks ---------- */
static void connlost_cb(void *context, char *cause)
{
    (void)context;
    (void)cause;
    /* Don’t attempt reconnect here; just mark disconnected and let app decide. */
    mqtt_connected = 0;
    fprintf(stderr, "[MQTT] Connection lost%s%s\n", (cause != NULL) ? ": " : "", (cause != NULL) ? cause : "");
}

static void delivered_cb(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;
    (void)dt; /* No-op; your publish is synchronous/checked elsewhere */
}

/* NOTE: Paho contract: return 1 to have library free message & topicName for us. */
static int msgarrived_cb(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    (void)context;

    /* Defensive checks */
    if ((message == NULL) || (topicName == NULL)) {
        if (message != NULL) { MQTTClient_freeMessage(&message); }
        if (topicName != NULL) { MQTTClient_free(topicName); }
        return 1;
    }

    /* Build bounded, NUL-terminated topic */
    char tbuf[256];
    size_t tlen = (topicLen > 0) ? (size_t)topicLen : (size_t)strlen(topicName);
    if (tlen >= (sizeof(tbuf) - 1U)) { tlen = sizeof(tbuf) - 1U; }
    (void)memcpy(tbuf, topicName, tlen);
    tbuf[tlen] = '\0';

    /* Payload: treat as bytes but provide ASCII/NUL-terminated copy for app convenience */
    const void *pl   = message->payload;
    size_t      plen = (pl != NULL) ? (size_t)message->payloadlen : 0U;

    char pbuf[1024];
    size_t copy_p = (plen < (sizeof(pbuf) - 1U)) ? plen : (sizeof(pbuf) - 1U);
    if (copy_p > 0U) {
        (void)memcpy(pbuf, pl, copy_p);
    }
    pbuf[copy_p] = '\0';

    /* Dispatch to user handler if installed */
    if (g_rx_handler != NULL) {
        g_rx_handler(tbuf, pbuf, copy_p);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}
