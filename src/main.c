#include <stdbool.h>
#include <time.h>

#define INCLUDE_CONF_IMPLEMENTATION
#include "conf.h"
#include "cum.h"
#include "flag.h"

#ifndef VERSION
#define VERSION "unknown"
#endif

#define DEFAULT_CONFIG "config.lua"

#define H(...) // header - for X-macros

#define LIST_OF_TASK_FIELDS()                                                  \
        H(name, required, type, luatype, defvalue, note)                       \
        X(name, true, const char *, str, NULL, "the name of the task")         \
        X(desc, false, const char *, str, NULL, "the description of the task") \
        X(timestamp, true, int, int, 0, "the deadline of the task")

typedef struct Task {
#define X(n, _, t, ...) t n;
        LIST_OF_TASK_FIELDS()
#undef X
} Task;

static struct {
        int quiet;
        Da(Task) tasks;
} g;

static void
#define X(n, _, t, ...) t n,
process_task(LIST_OF_TASK_FIELDS() int unused)
#undef X
{
        Unused(unused);
        Task t = {
#define X(n, ...) .n = n,
                LIST_OF_TASK_FIELDS()
#undef X
        };

        Da_append(&g.tasks, t);
}

static int
load_config(const char *config_path)
{
        Conf conf;
        int len;
#define X(_name, _required, _type, ...) _type _name;
        LIST_OF_TASK_FIELDS()
#undef X

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
#define X(_name, _required, _type, _luatype, _default, ...)                                            \
        do {                                                                                           \
                if (!_required) {                                                                      \
                        _name = _default;                                                              \
                        Conf_get_##_luatype(conf, &_name, "Tasks.%d." #_name, i);                      \
                } else {                                                                               \
                        if (Conf_get_##_luatype(conf, &_name, "Tasks.%d." #_name, i)) {                \
                                fprintf(stderr, "load_config: Tasks[%d]." #_name " is required\n", i); \
                                Conf_close(conf);                                                      \
                                return 1;                                                              \
                        }                                                                              \
                }                                                                                      \
        } while (0);
                LIST_OF_TASK_FIELDS()
#undef X

#define X(n, ...) n,
                process_task(LIST_OF_TASK_FIELDS() 0);
#undef X
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
                printf("tui-do version %s (%s %s)\n", VERSION, __DATE__, __TIME__);
                printf("Copyright (C) 2026 Hugo Coto\n");
                printf("This is free software: you are free to change and redistribute it.\n");
                printf("There is NO WARRANTY, to the extent permitted by law.\n");
                return 0;
        }

        g.quiet = quiet != NULL;

        ret = load_config(config_path);
        flag_free();
        return ret;
}
