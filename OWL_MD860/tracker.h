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

/* ---------- Tracker register map / constants ---------- */
#define OWL_ADDR_TRK_SET_COORD          (0x0001u)
#define OWL_ADDR_TRK_SET_BOX            (0x0002u)
#define OWL_ADDR_TRK_SET_MODE           (0x0003u)
#define OWL_ADDR_TRK_SET_LOCK           (0x0004u)
#define OWL_ADDR_TRK_SET_STAB           (0x0005u)
#define OWL_ADDR_TRK_SET_SZONE          (0x0006u)
#define OWL_ADDR_TRK_SET_AUTO           (0x0008u)
#define OWL_ADDR_TRK_SET_ROI            (0x0009u)
#define OWL_ADDR_TRK_SET_BITRATE        (0x000Cu)
#define OWL_ADDR_TRK_SET_MAXFAIL        (0x000Du)
#define OWL_ADDR_TRK_CAM_SWITCH         (0x000Eu)
#define OWL_ADDR_TRK_SET_TYPE           (0x000Fu)
#define OWL_ADDR_TRK_SET_SEARCH_AREA    (0x0010u)
#define OWL_ADDR_TRK_SET_CONFIDENCE     (0x0011u)

#define OWL_TRACK_CAM_THERMAL           (0x01u)
#define OWL_TRACK_CAM_DAY1              (0x02u)
#define OWL_TRACK_CAM_DAY2              (0x03u)

#define OWL_TRK_COORD_START             (0x01u)
#define OWL_TRK_COORD_STOP              (0x02u)
#define OWL_TRK_MODE_ON                 OWL_TRK_COORD_START
#define OWL_TRK_MODE_OFF                OWL_TRK_COORD_STOP

#define OWL_TRK_DETECT_DRONE            (0x03u)
#define OWL_TRK_DETECT_AIRCRAFT         (0x04u)
#define OWL_TRK_DETECT_NONE             (0x05u)
#define OWL_TRK_DETECT_ALL              (0x06u)
#define OWL_TRK_DETECT_BIRD             (0x07u)
#define OWL_TRK_DETECT_FIGHTERJET       (0x08u)

#define OWL_TRK_AUTO_NONE               (0x01u)
#define OWL_TRK_AUTO_DRONE              (0x05u)
#define OWL_TRK_AUTO_BIRD               (0x06u)
#define OWL_TRK_AUTO_FIGHTERJET         (0x07u)
#define OWL_TRK_AUTO_ALL                (0x08u)

#define OWL_TRK_DISABLE_THERMAL         (0x01u)
#define OWL_TRK_DISABLE_DAY1            (0x02u)
#define OWL_TRK_DISABLE_DAY2            (0x04u)

/* camera handle binder provided by camera_iface.c */
void tracker_bind_cam(void *cam_handle);

/* ---------- Adapter functions to your camera_iface (provide in your project) ----------
 * Return 0 on success, non-zero on failure.
 * NOTE: Implemented in tracker.c, using camera transport from camera_iface.c.
 */
int camera_iface_read_tracker(tracker_state_t *out_state);
int camera_iface_cmd_w6(uint8_t mode_value);
int camera_iface_cmd_w7(uint8_t disable_mask);

/* Camera tracker register API (implemented in tracker.c) */
int owl_tracker_mode_on(uint8_t cam, uint16_t x, uint16_t y);
int owl_tracker_mode_off(uint8_t cam, uint16_t x, uint16_t y);
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
