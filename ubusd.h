/**
 * @file ubusd.h
 * @brief iot-ubusd程序的主要数据结构和接口定义
 */

#ifndef __IOT_UBUSD_H__
#define __IOT_UBUSD_H__

#include <iot/mongoose.h>

/**
 * @brief 程序配置选项结构
 */
struct ubusd_option {
    const char *ubus_obj_cfg_file;    /**< ubus对象配置文件路径 */

    const char *mqtt_serve_address;      //mqtt 服务端口
    int mqtt_keepalive;                  //mqtt 保活间隔

    const char *module;
    const char *func;

    int debug_level;                  /**< 调试日志级别(0-4) */

};

/**
 * @brief 程序配置结构
 */
struct ubusd_config {
    struct ubusd_option *opts;    /**< 配置选项指针 */
    void *ubus_object_json;       /**< 解析后的JSON配置对象,退出时需要释放 */
};

/**
 * @brief 程序私有数据结构
 */
struct ubusd_private {
    struct ubusd_config cfg;      /**< 配置信息 */
    void *ubus_ctx;              /**< ubus上下文 */

    struct mg_mgr mgr;
    struct mg_connection *mqtt_conn;
    uint64_t ping_active;
    uint64_t pong_active;

    struct mg_fs *fs;            /**< mongoose文件系统操作接口 */

    int signo;                  /**< 退出信号 */

    volatile int request_full;   /**< 请求缓冲区是否已满 */
    volatile int response_full;  /**< 响应缓冲区是否已满 */
    char *request;     /**< 请求 */
    char *response;    /**< 响应 */
};

/**
 * @brief 程序主入口函数
 * @param user_options 用户配置选项
 * @return 0表示成功,其他值表示失败
 */
int ubusd_main(void *user_options);

#endif //__IOT_UBUSD_H__
