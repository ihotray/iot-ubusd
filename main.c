#include <iot/mongoose.h>
#include "ubusd.h"

#define UBUS_OBJECT_CONFIG_FILE "/www/iot/etc/iot-ubusd.json"

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
