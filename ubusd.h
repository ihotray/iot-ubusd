#ifndef __IOT_UBUSD_H__
#define __IOT_UBUSD_H__

#include <iot/mongoose.h>

struct ubusd_option {
    int debug_level;
    const char *ubus_obj_cfg_file;
};

struct ubusd_config {
    struct ubusd_option *opts;
    void *ubus_object_json; //json object, free it when exit
};

struct ubusd_private {

    struct ubusd_config cfg;
    void *ubus_ctx;
    struct mg_fs *fs;

};

int ubusd_main(void *user_options);

#endif //__IOT_UBUSD_H__