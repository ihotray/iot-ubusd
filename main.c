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
        "  -c PATH  - ubusd object config, default: '%s'\n"
        "  -v LEVEL - debug level, from 0 to 4, default: %d\n",
        MG_VERSION, prog, opts->ubus_obj_cfg_file, opts->debug_level);

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
        if (strcmp(argv[i], "-v") == 0) {
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
    };

    parse_args(argc, argv, &opts);

    MG_INFO(("IoT-SDK version         : v%s", MG_VERSION));
    MG_INFO(("Ubus object config file : %s", opts.ubus_obj_cfg_file));

    ubusd_main(&opts);

    return 0;
}
