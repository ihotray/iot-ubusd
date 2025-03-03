#include <iot/mongoose.h>
#include <iot/iot.h>
#include "ubusd.h"

#define IOT_UBUSD_PUB_TOPIC "mg/iot-ubusd/channel/iot-rpcd"
#define IOT_UBUSD_SUB_TOPIC "mg/iot-ubusd/channel"

static void mqtt_ev_open_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_INFO(("mqtt client connection created"));
}

static void mqtt_ev_error_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_ERROR(("%p %s", c->fd, (char *) ev_data));
    c->is_closing = 1;
}

static void mqtt_ev_poll_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;
    if (!priv->cfg.opts->mqtt_keepalive) //no keepalive
        return;

    uint64_t now = mg_millis();

    if (priv->pong_active && now > priv->pong_active &&
        now - priv->pong_active > (priv->cfg.opts->mqtt_keepalive + 3)*1000) { //TODO
        MG_INFO(("mqtt client connction timeout"));
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

static void mqtt_ev_close_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;
    MG_INFO(("mqtt client connection closed"));
    priv->mqtt_conn = NULL; // Mark that we're closed

}


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

static void mqtt_ev_mqtt_cmd_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;

    if (mm->cmd == MQTT_CMD_PINGRESP) {
        priv->pong_active = mg_millis();
    }
}

static void mqtt_ev_mqtt_msg_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct ubusd_private *priv = (struct ubusd_private*)c->mgr->userdata;

    MG_DEBUG(("received %.*s <- %.*s", (int) mm->data.len, mm->data.ptr,
        (int) mm->topic.len, mm->topic.ptr));

    // handle msg
    if ( !priv->response_full ) {
        priv->response = mg_mprintf("%.*s", (int) mm->data.len, mm->data.ptr);
        __sync_synchronize();
        priv->response_full = 1;
    }
}

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


// Timer function - recreate client connection if it is closed
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