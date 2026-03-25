/*
 * @file tracker.h
 * @brief Stateful MQTT listener & synchronizer for MD-400 Tracker (Table 10).
 *        Reads camera registers on startup, publishes STATE/<leaf>, listens on SET/<leaf>,
 *        diffs against held state, sends commands to camera, and on error,
 *        logs + resyncs by re-reading and re-publishing.
 * @author
 * @version 1.0
 * @date 2025-09-03
 * Compliant with MISRA C:2012 and ISO C11.
 */

#ifndef TRACKER_H
#define TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Public return codes ---------- */
typedef enum {
    TSM_OK = 0,
    TSM_ERR_BAD_ARG  = -1,
    TSM_ERR_IO       = -2
} tsm_status_t;

/* ---------- Tracker words we maintain (subset; extendable) ---------- */
typedef struct
{
    /* 0x0001 Tracker coordinates (6B) */
    uint8_t  coord_cam_id;   /* 1=Thermal, 2=Day */
    uint16_t coord_x;        /* ICD: range depends on box size */
    uint16_t coord_y;
    uint8_t  coord_mode;     /* 1=Start, 2=Stop */

    /* 0x0002 Box size (3B) */
    uint8_t  box_cam_id;
    uint8_t  box_w;          /* 23..250 */
    uint8_t  box_h;          /* 23..250 */

    /* 0x0003 Tracking mode (2B) */
    uint8_t  tmode_cam_id;
    uint8_t  tmode;          /* 1=Person,2=Vehicle,3=Drone */

    /* 0x0004 Target lock (2B) */
    uint8_t  lock_cam_id;
    uint8_t  lock_mode;      /* 1=Off,2=Auto,3=Reacquire */

    /* 0x0005 Stabilization (2B) */
    uint8_t  stab_cam_id;
    uint8_t  stab_mode;

    /* 0x0006 Selection zone (2B) */
    uint8_t  szone_cam_id;
    uint8_t  szone_zone;

    /* 0x0008 Auto detect (4B) */
    uint8_t  ad_cam_id;
    uint8_t  ad_mode;        /* 1=None,2=Vehicle,3=Aerial,4=Standard,5=Drone,6=Bird,7=FighterJet,8=All */
    uint8_t  ad_sens;        /* 1..10 */

    /* 0x0009 ROI mode (2B) */
    uint8_t  roi_cam_id;
    uint8_t  roi_mode;

    /* 0x000C Encoder bitrate (2B, centi-Mbps) */
    uint16_t enc_br_centi;

    /* 0x000D Max fail (2B) */
    uint8_t  mfail_cam_id;
    uint8_t  mfail_code;

    /* 0x000F Tracking Type (2B) */
    uint8_t  type_cam_id;
    uint8_t  type_code;      /* 1=Correlation,2=Edge,3=Centroid */

    /* 0x0010 Search Area (2B) */
    uint8_t  sarea_cam_id;
    uint8_t  sarea_percent;  /* 0-100% */

    /* 0x0011 Track Confidence (2B) */
    uint8_t  conf_cam_id;
    uint8_t  conf_tenths;    /* 0..10 means 0.0..1.0 */
} tracker_state_t;

/* ---------- Adapter functions to your camera_iface (provide in your project) ----------
 * Return 0 on success, non-zero on failure.
 * NOTE: These are declared here; define them in camera_iface.c (or adapt names).
 */
int camera_iface_read_tracker(tracker_state_t *out_state);
int camera_iface_cmd_w6(uint8_t mode_value);
int camera_iface_cmd_w7(uint8_t disable_mask);

/* Camera tracker register API (implemented in camera_iface.c) */
int owl_tracker_read_all(tracker_state_t *st);
int owl_tracker_set_coord(uint8_t cam, uint16_t x, uint16_t y, uint8_t mode);
int owl_tracker_set_box(uint8_t cam, uint8_t w, uint8_t h);
int owl_tracker_set_mode(uint8_t cam, uint8_t mode);
int owl_tracker_set_lock(uint8_t cam, uint8_t mode);
int owl_tracker_set_stab(uint8_t cam, uint8_t mode);
int owl_tracker_set_szone(uint8_t cam, uint8_t zone);
int owl_tracker_set_auto(uint8_t cam, uint8_t mode, uint8_t sens);
int owl_tracker_set_roi(uint8_t cam, uint8_t mode);
int owl_tracker_set_bitrate_centi(uint16_t centi_mbps);
int owl_tracker_set_maxfail(uint8_t cam, uint8_t code);
int owl_tracker_set_type(uint8_t cam, uint8_t type_code);
int owl_tracker_set_search_area(uint8_t cam, uint8_t percent);
int owl_tracker_set_confidence(uint8_t cam, uint8_t tenths);

/* ---------- MQTT adapter hooks (already in your mqtt.c/.h) -------------
 * Expected to exist in your repo. If names differ, change here & in tracker_sm.c.
 */
// int mqtt_publish_(const char *topic, const char *payload, int len);
int mqtt_subscribe(const char *topic);
void mqtt_set_rx_handler(void (*fn)(const char *topic, const char *payload, size_t len));

/**
 * @brief Initialize the tracker state machine.
 *        - Sets root (e.g., "OWL/TRACKER")
 *        - Subscribes to <root>/SET/#
 *        - Reads camera registers and publishes <root>/STATE/<leaf>
 * @param[in] root_topic Root topic; if NULL or empty, uses "OWL/TRACKER"
 * @param[in] refresh_period_ms periodic re-read/publish for health (e.g., 1000)
 * @return tsm_status_t
 */
tsm_status_t tracker_sm_init(const char *root_topic, uint32_t refresh_period_ms);

/**
 * @brief Poll function; call frequently (e.g., each main loop tick).
 *        Handles periodic resync if refresh elapsed.
 * @param[in] now_ms Monotonic milliseconds since boot (provide from your clock)
 * @return tsm_status_t
 */
tsm_status_t tracker_sm_poll(uint32_t now_ms);

/* ---------- Optional: get a snapshot of current held state ---------- */
tsm_status_t tracker_sm_get_state(tracker_state_t *out);
void tracker_set_udp_mcast(bool enabled, uint8_t camera_id, uint32_t *seq_counter);

#ifdef __cplusplus
}
#endif

#endif /* TRACKER_H */
