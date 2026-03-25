/*
 * @file camera_mqtt.c
 * @brief MQTT helpers for publishing/consuming camera parameters (Ethernet only).
 * @author Ishan
 * @version 1.0
 * @date 2025-09-01
 * Compliant with MISRA C:2012 and ISO C11.
 */

#include <stdint.h>
#include <limits.h>
#include "camera_mqtt.h"
#include "mqtt.h"         /* your existing wrapper */
#include "api2_mcast_win.h"
#include <string.h>
#include <stdio.h>


#define TOPIC_BASE_MAX   (128U)

/* local helpers */
static void publish_leaf_u32(const char *base, const char *leaf, uint32_t v);
static void publish_leaf_u16(const char *base, const char *leaf, uint16_t v);
static void publish_leaf_str(const char *base, const char *leaf, const char *s);

static int g_udp_mcast_enabled = 0;
static uint8_t g_udp_camera_id = 1u;
static uint32_t* g_udp_seq_counter = NULL;

static const char* _pbool(int set)
{
    return (set != 0) ? "true" : "false";
}

static void _mqtt_pub_flag_kv(const char *base, const char *name, const char *val)
{
    if ((base == NULL) || (name == NULL) || (val == NULL)) {
        return;
    }
    char topic[256];
    int n = snprintf(topic, sizeof(topic), "%s/%s", base, name);
    if ((n < 0) || ((size_t)n >= sizeof(topic))) {
        return; /* truncated -> skip publish */
    }
    (void)mqtt_publish(topic, (const void *)val, strlen(val));
    if ((g_udp_mcast_enabled != 0) && (g_udp_seq_counter != NULL)) {
        (void)api2_send_tlm1((*g_udp_seq_counter)++, g_udp_camera_id, topic, val);
    }
}

static void _mqtt_pub_flag_bool(const char *base, const char *name, int set)
{
    _mqtt_pub_flag_kv(base, name, _pbool(set));
}






bool camera_mqtt_connect(const camera_params_t *p)
{
    if (p == NULL) {
        return false;
    }

    /* Build tcp://<host>:<port> for mqtt_init() per your mqtt.h signature */
    char url[128];
    (void)snprintf(url, sizeof(url), "tcp://%s:%u", p->mqtt_broker, (unsigned)p->mqtt_port);

    if (mqtt_init(url, p->mqtt_client_id) != 0) {   /* connects internally */
        log_error("MQTT init/connect failed: %s", url);
        return false;
    }
    if (mqtt_is_connected() == 0) {
        log_error("MQTT not connected after init: %s", url);
        return false;
    }
    log_info("MQTT connected: %s (client=%s)", url, p->mqtt_client_id);
    return true;
}

void camera_mqtt_publish_all(const camera_params_t *p)
{
    if (p == NULL) {
        return;
    }

    char base[TOPIC_BASE_MAX];
    (void)snprintf(base, (size_t)TOPIC_BASE_MAX, "%s/CAMERA", p->root_topic); /* "OWL/CAMERA" */

    publish_leaf_str(base, "NAME", p->name);
    publish_leaf_str(base, "MODEL", p->model);
    publish_leaf_str(base, "SERIAL", p->serial);

    publish_leaf_u16(base, "WIDTH",  p->width);
    publish_leaf_u16(base, "HEIGHT", p->height);
    publish_leaf_u16(base, "FPS",    p->fps);

    publish_leaf_u32(base, "EXPOSURE_US", p->exposure_us);
    publish_leaf_u16(base, "GAIN_X10",    p->gain_x10);
    publish_leaf_u16(base, "WB_R",        p->wb_r);
    publish_leaf_u16(base, "WB_G",        p->wb_g);
    publish_leaf_u16(base, "WB_B",        p->wb_b);

    publish_leaf_str(base, "COLORSPACE", p->colorspace);

    /* Network diagnostics */
    publish_leaf_str(base, "MQTT_BROKER", p->mqtt_broker);
    publish_leaf_u16(base, "MQTT_PORT",   p->mqtt_port);
    publish_leaf_str(base, "MQTT_CLIENT_ID", p->mqtt_client_id);
    _mqtt_pub_flag_bool(base, "MQTT_RETAINED", (p->mqtt_retained ? 1 : 0));
}

static void publish_leaf_u32(const char *base, const char *leaf, uint32_t v)
{
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
    _mqtt_pub_flag_kv(base, leaf, buf);
}

static void publish_leaf_u16(const char *base, const char *leaf, uint16_t v)
{
    char buf[8];
    (void)snprintf(buf, sizeof(buf), "%u", (unsigned)v);
    _mqtt_pub_flag_kv(base, leaf, buf);
}

static void publish_leaf_str(const char *base, const char *leaf, const char *s)
{
    const char *val = (s != NULL) ? s : "";
    _mqtt_pub_flag_kv(base, leaf, val);
}

/* No inbound API in current mqtt.h; leave as no-op for now. */
void camera_mqtt_poll_and_process(camera_params_t *p)
{
    (void)p;
    static int warned = 0;
    if (warned == 0) {
        log_warning("%s", "camera_mqtt_poll_and_process: inbound CMD handling disabled (no subscribe API)");
        warned = 1;
    }
}

void camera_mqtt_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter)
{
    g_udp_mcast_enabled = (enabled != false) ? 1 : 0;
    g_udp_camera_id = camera_id;
    g_udp_seq_counter = seq_counter;
}
