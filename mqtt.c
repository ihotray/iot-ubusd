/**
 * @file mqtt.c
 * @brief MQTT client implementation for iot-ubusd
 * 
 * This file implements the MQTT client functionality for communication
 * between ubus and iot-rpcd through MQTT messages.
 */

#include <iot/mongoose.h>
#include <iot/iot.h>
#include "ubusd.h"

/* MQTT topic for publishing requests to iot-rpcd */
#define IOT_UBUSD_PUB_TOPIC "mg/iot-ubusd/channel/iot-rpcd"
/* MQTT topic for subscribing to responses */
#define IOT_UBUSD_SUB_TOPIC "mg/iot-ubusd/channel"

/**
 * @brief MQTT connection open event handler
 */
static void mqtt_ev_open_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_INFO(("mqtt client connection created"));
}

/**
 * @brief MQTT connection error event handler
 */
static void mqtt_ev_error_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_ERROR(("%p %s", c->fd, (char *) ev_data));
    c->is_closing = 1;
}

/**
 * @brief MQTT connection poll event handler
 * Handles periodic tasks: timeout checking and sending pending requests
 */
static void mqtt_ev_poll_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;
    if (!priv->cfg.opts->mqtt_keepalive) //no keepalive
        return;

    uint64_t now = mg_millis();

    if (priv->pong_active && now > priv->pong_active &&
        now - priv->pong_active > (priv->cfg.opts->mqtt_keepalive + 3)*1000) {
        MG_INFO(("mqtt client connection timeout"));
        c->is_draining = 1;
    }

    if (priv->request_full) {
        struct mg_str pubt = mg_str(IOT_UBUSD_PUB_TOPIC);
        struct mg_mqtt_opts pub_opts = {0};
        pub_opts.topic = pubt;
        pub_opts.message = mg_str(priv->request);
        pub_opts.qos = MQTT_QOS, pub_opts.retain = false;
        mg_mqtt_pub(c, &pub_opts);
        free(priv->request);
        priv->request = NULL;
        __sync_synchronize();
        priv->request_full = 0;
    }

}

/**
 * @brief MQTT connection close event handler
 */
static void mqtt_ev_close_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;
    MG_INFO(("mqtt client connection closed"));
    priv->mqtt_conn = NULL; // Mark that we're closed

}


/**
 * @brief MQTT connection established event handler
 * Subscribe to the response topic after connection is established
 */
static void mqtt_ev_mqtt_open_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_str subt = mg_str(IOT_UBUSD_SUB_TOPIC);

    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;

    MG_INFO(("connect to mqtt server: %s", priv->cfg.opts->mqtt_serve_address));
    struct mg_mqtt_opts sub_opts = {0};
    sub_opts.topic = subt;
    sub_opts.qos = MQTT_QOS;
    mg_mqtt_sub(c, &sub_opts);
    MG_INFO(("subscribed to %.*s", (int) subt.len, subt.ptr));

}

/**
 * @brief MQTT command event handler
 * Track PINGRESP messages for keepalive monitoring
 */
static void mqtt_ev_mqtt_cmd_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;

    if (mm->cmd == MQTT_CMD_PINGRESP) {
        priv->pong_active = mg_millis();
    }
}

/**
 * @brief MQTT message received event handler
 * Store the received response from iot-rpcd for the waiting ubus handler
 */
static void mqtt_ev_mqtt_msg_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;

    MG_DEBUG(("received %.*s <- %.*s", (int) mm->data.len, mm->data.ptr,
        (int) mm->topic.len, mm->topic.ptr));

    // handle msg - store response if buffer is available
    if ( !priv->response_full ) {
        priv->response = mg_mprintf("%.*s", (int) mm->data.len, mm->data.ptr);
        if (priv->response) {
            __sync_synchronize();
            priv->response_full = 1;
        }
    }
}

/**
 * @brief Main MQTT event handler callback
 * Dispatches events to specific handler functions
 */
static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    switch (ev) {
        case MG_EV_OPEN:
            mqtt_ev_open_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_ERROR:
            mqtt_ev_error_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_OPEN:
            mqtt_ev_mqtt_open_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_CMD:
            mqtt_ev_mqtt_cmd_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_MSG:
            mqtt_ev_mqtt_msg_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_POLL:
            mqtt_ev_poll_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_CLOSE:
            mqtt_ev_close_cb(c, ev, ev_data, fn_data);
            break;
    }
}


/**
 * @brief Timer callback function for MQTT connection management
 * 
 * This function:
 * 1. Recreates MQTT connection if it was closed
 * 2. Sends periodic PING messages for keepalive
 * 3. Handles system time changes
 */
void timer_mqtt_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    struct ubusd_private *priv = (struct ubusd_private*)mgr->userdata;
    uint64_t now = mg_millis();

    if (priv->mqtt_conn == NULL) {
        struct mg_mqtt_opts opts = { 0 };

        opts.clean = true;
        opts.qos = MQTT_QOS;
        opts.message = mg_str("goodbye");
        opts.keepalive = priv->cfg.opts->mqtt_keepalive;

        priv->mqtt_conn = mg_mqtt_connect(mgr, priv->cfg.opts->mqtt_serve_address, &opts, mqtt_cb, NULL);
        priv->ping_active = now;
        priv->pong_active = now;

    } else if (priv->cfg.opts->mqtt_keepalive) { //need keep alive
        
        if (now < priv->ping_active) {
            MG_INFO(("system time loopback"));
            priv->ping_active = now;
            priv->pong_active = now;
        }
        if (now - priv->ping_active >= priv->cfg.opts->mqtt_keepalive * 1000) {
            mg_mqtt_ping(priv->mqtt_conn);
            priv->ping_active = now;
        }
    }
}