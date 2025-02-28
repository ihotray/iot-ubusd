#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/ustream.h>
#include <libubox/utils.h>
#include <libubus.h>
#include <lualib.h>
#include <lauxlib.h>
#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include <iot/iot.h>
#include "ubusd.h"

#define LUA_IOT_RPC_SCRIPT "/www/iot/iot-rpc.lua"
#define LUA_CALLBACK_SCRIPT "/usr/share/iot/rpc/ubus/iot-ubusd.lua"

static void signal_handler(int signo) {
    uloop_end();
}

static void do_handler(const char *object, const char *method, const char *param, struct mg_str *out) {
    const char *response = NULL, *error_msg = NULL;
    bool rpc = false;
    int ret = 0;
    const char *lua_path = LUA_CALLBACK_SCRIPT;
    const char *func = "call";
    cJSON *root = NULL;
    lua_State *L = NULL;

    L = luaL_newstate();
    luaL_openlibs(L);

    if (strcmp(object, "iot-ubusd") == 0 && strcmp(method, "iot-rpc") == 0) {
        rpc = true;
        lua_path = LUA_IOT_RPC_SCRIPT;
    }

    if (luaL_dofile(L, lua_path)) {
        MG_ERROR(("open lua file %s failed", lua_path));
        error_msg = "{\"code\": -1, \"msg\": \"callback not found\"}\n";
        goto done;
    }

    if (rpc) {
        root = cJSON_Parse(param);
        cJSON *obj = cJSON_GetObjectItem(root, "method");
        if (cJSON_IsString(obj)) {
            func = cJSON_GetStringValue(obj);
        }
    }

    lua_getfield(L, -1, func);
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method %s is not a function", "call"));
        error_msg = "{\"code\": -1, \"msg\": \"method is unsupported\"}\n";
        goto done;
    }

    if (!rpc) {
        lua_pushstring(L, object);
        lua_pushstring(L, method);
    }
    lua_pushstring(L, param);

    if (rpc) {
        ret = lua_pcall(L, 1, 1, 0); // one param, one return values, zero error func
    } else {
        ret = lua_pcall(L, 3, 1, 0); // three param, one return values, zero error func
    }

    if (ret) {
        MG_ERROR(("lua call failed"));
        error_msg = "{\"code\": -1, \"msg\": \"call failed\"}\n";
        goto done;
    }

    response = lua_tostring(L, -1);
    if (NULL == response) {
        MG_ERROR(("lua call no response"));
        error_msg = "{\"code\": -1, \"msg\": \"no response\"}\n";
    }

done:
    if (!response)
        response = error_msg;

    if (root)
        cJSON_Delete(root);

    if (L)
        lua_close(L);

    //free by caller
    *out = mg_strdup(mg_str(response));
}

static int ubus_handler(struct ubus_context *ctx, struct ubus_object *obj,
                    struct ubus_request_data *req, const char *method,
                    struct blob_attr *msg) {
    struct blob_buf bb;
    const char *response = NULL;

    char *json_msg = blobmsg_format_json(msg, true);

    MG_DEBUG(("ubus call object: %s, method: %s, param: %s", obj->name, method, json_msg));

    struct mg_str out = {0};

    if (json_msg)
        do_handler(obj->name, method, json_msg, &out);

    if (!out.ptr || out.len == 0) {
        response = "{\"code\": -1, \"msg\": \"no data\"}\n";
    } else {
        response = out.ptr;
    }

    memset(&bb, 0, sizeof(bb));
    blob_buf_init(&bb, 0);

    blobmsg_add_json_from_string(&bb, response);

    ubus_send_reply(ctx, req, bb.head);
    blob_buf_free(&bb);

    if (out.ptr)
        free((void*)out.ptr);
    
    if (json_msg)
        free((void*)json_msg);

    return 0;
}

#define UBUS_METHOD_ADD(_tab, iter, __m)                          \
    do                                                            \
    {                                                             \
        struct ubus_method ___m = __m;                            \
        memcpy(&_tab[iter++], &___m, sizeof(struct ubus_method)); \
    } while (0)

static int blogmsg_type(const char *type) {
    if (strcmp(type, "BLOBMSG_TYPE_STRING") == 0) {
        return BLOBMSG_TYPE_STRING;
    } else if (strcmp(type, "BLOBMSG_TYPE_INT32") == 0) {
        return BLOBMSG_TYPE_INT32;
    } else if (strcmp(type, "BLOBMSG_TYPE_BOOL") == 0) {
        return BLOBMSG_TYPE_BOOL;
    } else if (strcmp(type, "BLOBMSG_TYPE_TABLE") == 0) {
        return BLOBMSG_TYPE_TABLE;
    } else if (strcmp(type, "BLOBMSG_TYPE_ARRAY") == 0) {
        return BLOBMSG_TYPE_ARRAY;
    } else if (strcmp(type, "BLOBMSG_TYPE_UNSPEC") == 0) {
        return BLOBMSG_TYPE_UNSPEC;
    } else {
        return BLOBMSG_TYPE_UNSPEC;
    }
}

static int add_methods(struct ubus_object *obj, cJSON *method) {
    int n_methods = 0;
    size_t n_ubus_methods = cJSON_GetArraySize(method);

    struct ubus_method *ubus_methods = calloc(n_ubus_methods, sizeof(struct ubus_method));
    if (!ubus_methods)
        return -ENOMEM;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, method) {
        cJSON *name = cJSON_GetObjectItem(item, "name");
        if (!cJSON_IsString(name))
            continue;
        cJSON *param = cJSON_GetObjectItem(item, "param");
        struct blobmsg_policy *policy = NULL;
        int n_policy = cJSON_GetArraySize(param);
        if (n_policy > 0) {
            policy = calloc(n_policy, sizeof(struct blobmsg_policy));
            if (!policy)
                return -ENOMEM;
            int i = 0;
            cJSON *param_item = NULL;
            cJSON_ArrayForEach(param_item, param) {
                cJSON *type = cJSON_GetObjectItem(param_item, "type");
                cJSON *name = cJSON_GetObjectItem(param_item, "name");
                if (cJSON_IsString(type) && cJSON_IsString(name)) {
                    policy[i].type = blogmsg_type(cJSON_GetStringValue(type));
                    policy[i].name = cJSON_GetStringValue(name);
                }
                i++;
            }
        }
        struct ubus_method m = {
            .name = cJSON_GetStringValue(name),
            .policy = policy,
            .n_policy = n_policy,
            .handler = ubus_handler,
            .mask = 0,
            .tags = 0,
        };
        UBUS_METHOD_ADD(ubus_methods, n_methods, m);
        MG_INFO(("add ubus object: %s, method: %s, param size: %d", obj->name, m.name, m.n_policy));
    }

    obj->methods = ubus_methods;
    obj->n_methods = n_methods;

    return 0;
}

static int add_object(void *handle, const char *objname, int (*add_methods)(struct ubus_object *o, cJSON *method), cJSON *method) {
    struct ubus_object *obj = NULL;
    struct ubus_object_type *obj_type = NULL;
    struct ubusd_private *priv = (struct ubusd_private *)handle;
    struct ubus_context *ctx = priv->ubus_ctx;

    obj = calloc(1, sizeof(struct ubus_object));
    if (!obj)
        return -ENOMEM;

    obj_type = calloc(1, sizeof(struct ubus_object_type));
    if (!obj_type) {
        free(obj);
        return -ENOMEM;
    }

    obj->name = objname;
    if (add_methods)
        add_methods(obj, method);

    obj_type->name = obj->name;
    obj_type->n_methods = obj->n_methods;
    obj_type->methods = obj->methods;
    obj->type = obj_type;

    return ubus_add_object(ctx, obj);
}

static void add_objects(void *handle) {
    struct ubusd_private *priv = (struct ubusd_private *)handle;
    size_t file_size = 0;
    priv->fs->st(priv->cfg.opts->ubus_obj_cfg_file, &file_size, NULL);
    size_t align_file_size = ((file_size + 1) / 64 + 1) * 64; //align 64 bytes
    MG_INFO(("load config file: %s, size: %d(%d)", priv->cfg.opts->ubus_obj_cfg_file, file_size, align_file_size));
    void *fp = priv->fs->op(priv->cfg.opts->ubus_obj_cfg_file, MG_FS_READ);
    if (fp) {
        char *buf = calloc(1, align_file_size);
        size_t size = priv->fs->rd(fp, buf, align_file_size - 1);
        cJSON *root = cJSON_ParseWithLength(buf, size);
        if (root && cJSON_IsArray(root)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, root) {
                cJSON *object = cJSON_GetObjectItem(item, "object");
                cJSON *method = cJSON_GetObjectItem(item, "method");
                if (object && cJSON_IsString(object) && method && cJSON_IsArray(method)) {
                    add_object(handle, cJSON_GetStringValue(object), add_methods, method);
                } else {
                    MG_ERROR(("config file %s format is wrong", priv->cfg.opts->ubus_obj_cfg_file));
                }
            }
            priv->cfg.ubus_object_json = root;
        } else {
            MG_ERROR(("config file %s format is wrong", priv->cfg.opts->ubus_obj_cfg_file));
            if (root)
                cJSON_Delete(root);
        }

        free(buf);
        priv->fs->cl(fp);
    } else {
        MG_ERROR(("cannot open config file: %s", priv->cfg.opts->ubus_obj_cfg_file));
    }
}

int ubusd_init(void **priv, void *opts) {

    struct ubusd_private *p;
    struct ubus_context *ctx = NULL;

    signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM

    *priv = NULL;
    p = calloc(1, sizeof(struct ubusd_private));
    if (!p)
        return -1;

    p->cfg.opts = opts;
    mg_log_set(p->cfg.opts->debug_level);
    p->fs = &mg_fs_posix;

    uloop_init();
    ctx = ubus_connect(NULL);
    if (!ctx) {
        MG_ERROR(("failed to connect to ubus"));
        return -1;
    }

    ubus_add_uloop(ctx);
    p->ubus_ctx = ctx;

    // add ubus objects
    add_objects(p);

    *priv = p;

    return 0;

}

void ubusd_run() {
    uloop_run();
}

void ubusd_exit(void *handle) {
    struct ubusd_private *priv = (struct ubusd_private *)handle;
    ubus_free(priv->ubus_ctx);
    uloop_done();
    if (priv->cfg.ubus_object_json)
        cJSON_Delete(priv->cfg.ubus_object_json);
    free(handle);
}

int ubusd_main(void *user_options) {

    struct ubusd_option *opts = (struct ubusd_option *)user_options;
    void *ubusd_handle;
    int ret;

    ret = ubusd_init(&ubusd_handle, opts);
    if (ret)
        exit(EXIT_FAILURE);

    ubusd_run();

    ubusd_exit(ubusd_handle);

    return 0;

}
