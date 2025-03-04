/**
 * @file ubusd.c
 * @brief iot-ubusd的核心实现文件
 * 
 * 该文件实现了:
 * 1. ubus对象和方法的动态注册
 * 2. ubus请求的处理和转发
 * 3. Lua回调脚本的调用
 * 4. 错误处理和资源清理
 */

#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/ustream.h>
#include <libubox/utils.h>
#include <libubus.h>
#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include <iot/iot.h>
#include "ubusd.h"

struct ubus_object_ext {
    struct ubus_object obj;
    void *priv;
};

static int *s_signo = NULL;

/**
 * @brief 信号处理函数
 * @param signo 信号编号
 */
static void signal_handler(int signo) {
    *s_signo = signo;
    uloop_end();
}

/**
 * @brief ubus请求处理回调函数
 * @param ctx ubus上下文
 * @param obj ubus对象
 * @param req 请求数据
 * @param method 调用的方法名
 * @param msg 请求参数
 * @return 0表示成功,其他值表示失败
 * 
 * 该函数负责:
 * 1. 将blob格式参数转换为JSON字符串
 * 2. 调用do_handler处理请求
 * 3. 将处理结果转换回blob格式并响应
 */
static int ubus_handler(struct ubus_context *ctx, struct ubus_object *obj,
                    struct ubus_request_data *req, const char *method,
                    struct blob_attr *msg) {
    struct blob_buf bb;
    const char *response = NULL;
    struct ubus_object_ext *obj_ext = container_of(obj, struct ubus_object_ext, obj);
    struct ubusd_private *priv = (struct ubusd_private *)obj_ext->priv;

    char *json_msg = blobmsg_format_json(msg, true);

    MG_DEBUG(("ubus call object: %s, method: %s, param: %s", obj->name, method, json_msg));

    char *out = NULL;

    if (json_msg) {
        if (strcmp(obj->name, "iot-ubusd") != 0 || strcmp(method, "iot-rpc") != 0) { // not iot-rpc
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, FIELD_METHOD, "call");

            cJSON *param = cJSON_CreateArray();
            cJSON_AddItemToArray(param, cJSON_CreateString(priv->cfg.opts->module));
            cJSON_AddItemToArray(param, cJSON_CreateString(priv->cfg.opts->func));

            cJSON *args = cJSON_CreateObject();
            cJSON_AddItemToObject(args, "object", cJSON_CreateString(obj->name));
            cJSON_AddItemToObject(args, "method", cJSON_CreateString(method));
            cJSON *data_obj = cJSON_Parse(json_msg);
            cJSON_AddItemToObject(args, FIELD_DATA, data_obj);
            cJSON_AddItemToArray(param, args);

            cJSON_AddItemToObject(root, FIELD_PARAM, param);
            free(json_msg);
            json_msg = cJSON_Print(root);
        }
        while ( priv->request_full && priv->signo == 0 ) {
            usleep(1000);
        }

        if ( !priv->request_full ) {
            priv->request = strdup(json_msg);
            __sync_synchronize();
            priv->request_full = 1;
        }

        // TIMEOUT, 10S, 1000*10ms
        int try = 0;
        while ( !priv->response_full && try++ < 1000 && priv->signo == 0 ) {
            usleep(10000);
        }

        if ( priv->response_full ) {
            out = strdup(priv->response);
            free(priv->response);
            priv->response = NULL;
            __sync_synchronize();
            priv->response_full = 0;
        }
    }
    if (!out) {
        response = "{\"code\": -1, \"msg\": \"no data\"}\n";
    } else {
        response = out;
    }

    memset(&bb, 0, sizeof(bb));
    blob_buf_init(&bb, 0);

    blobmsg_add_json_from_string(&bb, response);

    ubus_send_reply(ctx, req, bb.head);
    blob_buf_free(&bb);

    if (out)
        free(out);

    if (json_msg)
        free(json_msg);

    return 0;
}

#define UBUS_METHOD_ADD(_tab, iter, __m)                          \
    do                                                            \
    {                                                             \
        struct ubus_method ___m = __m;                            \
        memcpy(&_tab[iter++], &___m, sizeof(struct ubus_method)); \
    } while (0)

/**
 * @brief 将字符串类型转换为blobmsg类型
 * @param type 类型字符串
 * @return blobmsg类型枚举值
 */
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

/**
 * @brief 向ubus对象添加方法
 * @param obj ubus对象
 * @param method JSON格式的方法定义
 * @return 0表示成功,其他值表示失败
 * 
 * 该函数负责:
 * 1. 解析JSON中的方法定义
 * 2. 创建ubus_method结构
 * 3. 设置方法的处理函数和参数策略
 */
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

/**
 * @brief 添加ubus对象
 * @param handle 程序句柄
 * @param objname 对象名称
 * @param add_methods 添加方法的回调函数
 * @param method JSON格式的方法定义
 * @return 0表示成功,其他值表示失败
 * 
 * 该函数负责:
 * 1. 创建ubus对象和类型结构
 * 2. 调用add_methods添加方法
 * 3. 向ubus注册对象
 */
static int add_object(void *handle, const char *objname, int (*add_methods)(struct ubus_object *o, cJSON *method), cJSON *method) {
    struct ubus_object_ext *obj_ext = NULL;
    struct ubus_object *obj = NULL;
    struct ubus_object_type *obj_type = NULL;
    struct ubusd_private *priv = (struct ubusd_private *)handle;
    struct ubus_context *ctx = priv->ubus_ctx;

    obj_ext = calloc(1, sizeof(struct ubus_object_ext));
    if (!obj_ext)
        return -ENOMEM;

    obj_ext->priv = handle;
    obj = &obj_ext->obj;

    obj_type = calloc(1, sizeof(struct ubus_object_type));
    if (!obj_type) {
        free(obj_ext);
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

/**
 * @brief 从配置文件加载并添加所有ubus对象
 * @param handle 程序句柄
 * 
 * 该函数负责:
 * 1. 读取JSON配置文件
 * 2. 解析对象和方法定义
 * 3. 调用add_object注册每个对象
 */
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

static void start_thread(void *(*f)(void *), void *p) {
#ifdef _WIN32
#define usleep(x) Sleep((x) / 1000)
    _beginthread((void(__cdecl *)(void *))f, 0, p);
#else
#include <pthread.h>
    pthread_t thread_id = (pthread_t)0;
    pthread_attr_t attr;
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&thread_id, &attr, f, p);
    pthread_attr_destroy(&attr);
#endif
}

void timer_mqtt_fn(void *arg);
static void *mgr_thread(void *param) {
    struct ubusd_private *priv = (struct ubusd_private *)param;
    int timer_opts = MG_TIMER_REPEAT | MG_TIMER_RUN_NOW;

    mg_mgr_init(&priv->mgr);
    priv->mgr.userdata = priv;
    mg_timer_add(&priv->mgr, 2000, timer_opts, timer_mqtt_fn, &priv->mgr);
    while (priv->signo == 0) mg_mgr_poll(&priv->mgr, 10);  // Event loop, 10ms timeout

    return NULL;
}

/**
 * @brief 初始化iot-ubusd服务
 * @param priv 返回程序私有数据指针
 * @param opts 配置选项
 * @return 0表示成功,其他值表示失败
 * 
 * 该函数负责:
 * 1. 初始化信号处理
 * 2. 创建程序私有数据结构
 * 3. 连接ubus
 * 4. 加载并注册ubus对象
 */
int ubusd_init(void **priv, void *opts) {

    struct ubusd_private *p;
    struct ubus_context *ctx = NULL;

    *priv = NULL;
    p = calloc(1, sizeof(struct ubusd_private));
    if (!p)
        return -1;

    s_signo = &p->signo;
    signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM

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

    // start mgr thread
    start_thread(mgr_thread, p);

    *priv = p;

    return 0;

}

/**
 * @brief 运行iot-ubusd服务主循环
 * 
 * 启动uloop事件循环,处理ubus请求
 */
void ubusd_run() {
    uloop_run();
}

/**
 * @brief 清理并退出iot-ubusd服务
 * @param handle 程序句柄
 * 
 * 该函数负责:
 * 1. 释放ubus上下文
 * 2. 释放JSON配置对象
 * 3. 释放程序私有数据
 */
void ubusd_exit(void *handle) {
    struct ubusd_private *priv = (struct ubusd_private *)handle;
    ubus_free(priv->ubus_ctx);
    uloop_done();
    if (priv->cfg.ubus_object_json)
        cJSON_Delete(priv->cfg.ubus_object_json);

    free(handle);
}

/**
 * @brief iot-ubusd服务主函数
 * @param user_options 用户配置选项
 * @return 0表示成功,其他值表示失败
 * 
 * 该函数负责协调整个服务的:
 * 1. 初始化
 * 2. 运行
 * 3. 退出清理
 */
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
