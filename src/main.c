#define INCLUDE_CONF_IMPLEMENTATION
#include "conf.h"
#include "flag.h"

#ifndef VERSION
#define VERSION "unknown"
#endif

#define DEFAULT_CONFIG "config.lua"

static struct {
        int quiet;
} g;

static void
process_task(const char *name, const char *desc, int timestamp)
{
        if (g.quiet) return;
        printf("task: name=%s desc=%s timestamp=%d\n", name, desc ? desc : "(none)", timestamp);
}

static int
load_config(const char *config_path)
{
        Conf conf;
        const char *name, *desc;
        int len, timestamp;

        if (Conf_open(&conf, config_path) != CONF_OK) {
                fprintf(stderr, "load_config: could not open '%s'\n", config_path);
                return 1;
        }

        if (Conf_get_len(conf, &len, "Tasks") != CONF_OK) {
                fprintf(stderr, "load_config: 'Tasks' not found in '%s'\n", config_path);
                Conf_close(conf);
                return 1;
        }

        for (int i = 1; i <= len; i++) {
                if (Conf_get_str(conf, &name, "Tasks.%d.name", i) != CONF_OK) {
                        fprintf(stderr, "load_config: Tasks[%d].name is required\n", i);
                        Conf_close(conf);
                        return 1;
                }
                if (Conf_get_int(conf, &timestamp, "Tasks.%d.timestamp", i) != CONF_OK) {
                        fprintf(stderr, "load_config: Tasks[%d].timestamp is required\n", i);
                        Conf_close(conf);
                        return 1;
                }
                desc = NULL;
                Conf_get_str(conf, &desc, "Tasks.%d.desc", i);
                process_task(name, desc, timestamp);
        }

        Conf_close(conf);
        return 0;
}

int
main(int argc, char **argv)
{
        const char *version, *config_path, *quiet;
        int ret;

        flag_program(.name = "tui-do", .help = "A terminal todo manager");
        flag_add(&version, "--version", .help = "Show version and exit");
        flag_add(&config_path, "--config", .nargs = 1, .defaults = DEFAULT_CONFIG,
                 .help = "Path to config file");
        flag_add(&quiet, "--quiet", .help = "Suppress task output");

        if (flag_parse(&argc, &argv)) {
                flag_show_help(STDOUT_FILENO);
                flag_free();
                return 1;
        }

        if (version) {
                printf("tui-do %s\n", VERSION);
                flag_free();
                return 0;
        }

        g.quiet = quiet != NULL;

        ret = load_config(config_path);
        flag_free();
        return ret;
}
