/**
 * @file main.c
 * @brief iot-ubusd主程序入口文件
 * 
 * 该文件负责程序的参数解析和初始化工作：
 * 1. 解析命令行参数
 * 2. 初始化日志级别
 * 3. 启动ubusd主服务
 */

#include <iot/mongoose.h>
#include "ubusd.h"

/* ubus对象配置文件默认路径 */
#define UBUS_OBJECT_CONFIG_FILE "/www/iot/etc/iot-ubusd.json"
/* Lua脚本路径定义 */
#define LUA_RPC_SCRIPT "/www/iot/iot-rpc.lua"           /**< IoT RPC处理脚本 */
#define LUA_CALLBACK_SCRIPT "/usr/share/iot/rpc/ubus/iot-ubusd.lua"  /**< 标准ubus调用处理脚本 */

/**
 * @brief 打印程序使用帮助信息
 * @param prog 程序名称
 * @param default_opts 默认配置选项
 */
static void usage(const char *prog, struct ubusd_option *default_opts) {
    struct ubusd_option *opts = default_opts;
    fprintf(stderr,
        "IoT-SDK v.%s\n"
        "Usage: %s OPTIONS\n"
        "  -x PATH  - lua rpc script, default: '%s'\n"
        "  -X PATH  - lua callback script, default: '%s'\n"
        "  -c PATH  - ubusd object config, default: '%s'\n"
        "  -v LEVEL - debug level, from 0 to 4, default: %d\n",
        MG_VERSION, prog, opts->lua_rpc_script, opts->lua_callback_script, opts->ubus_obj_cfg_file, opts->debug_level);

    exit(EXIT_FAILURE);
}

/**
 * @brief 解析命令行参数
 * @param argc 参数个数
 * @param argv 参数数组
 * @param opts 配置选项结构体
 * 
 * 支持的参数:
 * -v: 设置调试级别(0-4)
 * -c: 设置ubus对象配置文件路径
 */
static void parse_args(int argc, char *argv[], struct ubusd_option *opts) {
    // Parse command-line flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0) {
            opts->lua_rpc_script = argv[++i];
        } else if (strcmp(argv[i], "-X") == 0) {
            opts->lua_callback_script = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            opts->debug_level = atoi(argv[++i]);
        } else if( strcmp(argv[i], "-c") == 0) {
            opts->ubus_obj_cfg_file = argv[++i];
        } else {
            usage(argv[0], opts);
        }
    }
}

/**
 * @brief 主程序入口
 * @param argc 参数个数 
 * @param argv 参数数组
 * @return 0表示成功，其他值表示失败
 */
int main(int argc, char *argv[]) {

    struct ubusd_option opts = {
        .debug_level = MG_LL_INFO,
        .ubus_obj_cfg_file = UBUS_OBJECT_CONFIG_FILE,
        .lua_callback_script = LUA_CALLBACK_SCRIPT,
        .lua_rpc_script = LUA_RPC_SCRIPT
    };

    parse_args(argc, argv, &opts);

    MG_INFO(("IoT-SDK version         : v%s", MG_VERSION));
    MG_INFO(("Ubus object config file : %s", opts.ubus_obj_cfg_file));

    ubusd_main(&opts);

    return 0;
}
