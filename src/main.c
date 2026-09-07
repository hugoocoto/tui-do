#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define INCLUDE_CONF_IMPLEMENTATION
#include "conf.h"
#include "cum.h"
#include "flag.h"

#ifndef VERSION
#define VERSION "unknown"
#endif

#define TAB "%*.*s"                      // tab format for printf
#define TAB_C g.tab_size, g.tab_size, "" // tab arguments for printf

#define printf(fmt, ...)                                   \
        do {                                               \
                if (g.verbose) printf(fmt, ##__VA_ARGS__); \
        } while (0)

#define C "\033[%sm"

#define H(...) // header - for X-macros

#define LIST_OF_TASK_FIELDS()                                                          \
        H(name, required, type, luatype, defvalue, copy, note)                         \
        X(name, true, const char *, str, NULL, strdup, "the name of the task")         \
        X(desc, false, const char *, str, NULL, strdup, "the description of the task") \
        X(date, true, int, int, 0, ((int (*)(int)) 0), "the deadline of the task")

typedef struct Task {
#define X(n, _, t, ...) t n;
        LIST_OF_TASK_FIELDS()
#undef X
        bool overdued;
} Task;

static struct {
        Da(Task) tasks;
        bool verbose;
        bool pretty;
        bool remaining;
        bool has_until;
        time_t until;
        int tab_size;

        struct {
                const char *rst;
                const char *da;
                const char *od;
                const char *op;
                const char *na;
                const char *de;
        } c;
        char *default_config;
        time_t now;
} g = {
        .c.rst = "0",  // no modificable
        .c.da  = "32", // date - green
        .c.od  = "31", // overdued date - red
        .c.op  = "39", // operators - white
        .c.na  = "39", // name - white
        .c.de  = "30", // description - light white
};

static void
#define X(n, _, t, ...) t n,
task_append(LIST_OF_TASK_FIELDS() int unused)
#undef X
{
        Unused(unused);
        Task t = {
#define X(n, ...) .n = n,
                LIST_OF_TASK_FIELDS()
#undef X
        };
        t.overdued = t.date <= g.now;
        Da_append(&g.tasks, t);
}

int
task_compare(const void *t1, const void *t2)
{
        return ((Task *) t1)->date - ((Task *) t2)->date;
}

void
tasks_sort()
{
        qsort(g.tasks.items, g.tasks.count, sizeof *g.tasks.items, task_compare);
}

int
mkdirp(const char *path, mode_t perms)
{
        char *c;
        char *p;
        int s;

        c = p = strdup(path);
        while ((c = strchr(c + 1, '/'))) {
                *c = 0;
                s  = mkdir(p, perms);
                *c = '/';
                if (s && errno != EEXIST) {
                        free(p);
                        return s;
                }
        }

        if (!c) {
                s = mkdir(p, perms);
                if (s && errno != EEXIST) {
                        free(p);
                        return s;
                }
        }

        free(p);
        return 0;
}

static void
task_dump(const char *filename)
{
        char *path = dirname(strdup(filename));
        mkdirp(path, 755);
        free(path);

        FILE *f = fopen(filename, "w");
        if (!f) {
                fprintf(stderr, "Can not dump tasks to file %s\n", filename);
                return;
        }

        fprintf(f, "Tasks = {\n");
        Da_foreach(task, g.tasks)
        {
                fprintf(f, TAB "{\n", TAB_C);
                fprintf(f, TAB TAB "name = \"%s\",\n", TAB_C, TAB_C, task->name);
                if (task->desc) fprintf(f, TAB TAB "desc = \"%s\",\n", TAB_C, TAB_C, task->desc);

                {
                        time_t t      = (time_t) task->date;
                        struct tm *tm = localtime(&t);
                        fprintf(f, TAB TAB "date = os.time({ ", TAB_C, TAB_C);
                        if (tm->tm_year) fprintf(f, "year = %d, ", tm->tm_year + 1900);
                        if (tm->tm_mon) fprintf(f, "month = %d, ", tm->tm_mon + 1);
                        if (tm->tm_mday) fprintf(f, "day = %d, ", tm->tm_mday);
                        if (tm->tm_hour) fprintf(f, "hour = %d, ", tm->tm_hour);
                        if (tm->tm_min) fprintf(f, "min = %d, ", tm->tm_min);
                        if (tm->tm_sec) fprintf(f, "sec = %d, ", tm->tm_sec);
                        fprintf(f, "}),\n");
                }

                fprintf(f, TAB "},\n", TAB_C);
        }
        fprintf(f, "}\n");

        fclose(f);
}

static char *
trim(char *str, char chr)
{
        char *c;
        if ((c = strchr(str, chr))) {
                *c = 0;
        }
        return str;
}

static void
tasks_free()
{
        Da_foreach(task, g.tasks)
        {
                free((void *) task->name);
                free((void *) task->desc);
        }
        Da_destroy(&g.tasks);
}

#define SECS_PER_MIN 60
#define SECS_PER_HOUR (60 * SECS_PER_MIN)
#define SECS_PER_DAY (24 * SECS_PER_HOUR)
#define SECS_PER_YEAR (365 * SECS_PER_DAY)

static time_t
time_diff(time_t a, time_t b)
{
        return a - b;
}

/* Formats diff (seconds) as "  1d  3h 00m 25s": leading zero fields are
 * blanked out (instead of omitted) so every call returns a same-length,
 * column-aligned string. A negative diff (overdue) is formatted as its
 * absolute value prefixed with '-', e.g. "-  1d  3h 00m 25s". */
static char *
format_remaining(time_t diff)
{
        static const int width[5] = { 3, 3, 2, 2, 2 };
        static const char unit[5] = { 'y', 'd', 'h', 'm', 's' };
        static char buf[32];
        bool neg  = diff < 0;
        long secs = neg ? -(long) diff : (long) diff;
        long v[5];
        int i, n = 0, start;

        v[0] = secs / SECS_PER_YEAR;
        secs %= SECS_PER_YEAR;
        v[1] = secs / SECS_PER_DAY;
        secs %= SECS_PER_DAY;
        v[2] = secs / SECS_PER_HOUR;
        secs %= SECS_PER_HOUR;
        v[3] = secs / SECS_PER_MIN;
        secs %= SECS_PER_MIN;
        v[4] = secs;

        for (start = 0; start < 4 && v[start] == 0; start++)
                ;

        buf[n++] = neg ? '-' : ' ';

        for (i = 0; i < 5; i++) {
                if (i < start)
                        n += snprintf(buf + n, sizeof buf - n, "%*s", width[i] + 1, "");
                else if (i == start)
                        n += snprintf(buf + n, sizeof buf - n, "%*ld%c", width[i], v[i], unit[i]);
                else
                        n += snprintf(buf + n, sizeof buf - n, "%0*ld%c", width[i], v[i], unit[i]);
                if (i < 4) buf[n++] = ' ';
        }
        buf[n] = 0;

        return buf;
}

static char *
date_str(time_t t)
{
        if (g.remaining) return format_remaining(time_diff(t, g.now));
        return trim(ctime(&t), '\n');
}

static void
tasks_print()
{
        Da_foreach(task, g.tasks)
        {
                time_t t = (time_t) task->date;
                if (g.has_until && t >= g.until) continue;
                if (g.pretty) {
                        fprintf(stdout, C "[" C "%s" C "]" C " " C "%s" C,
                                g.c.op, task->overdued ? g.c.od : g.c.da, date_str(t),
                                g.c.op, g.c.rst, g.c.na, task->name, g.c.rst);
                        if (task->desc && task->desc[0]) {
                                fprintf(stdout, C ":" C " " C "%s" C,
                                        g.c.op, g.c.rst, g.c.de,
                                        task->desc, g.c.rst);
                        }
                        fprintf(stdout, "\n");
                } else {
                        fprintf(stdout, "[%s] %s", date_str(t), task->name);
                        if (task->desc && task->desc[0]) {
                                fprintf(stdout, ": %s", task->desc);
                        }
                        fprintf(stdout, "\n");
                }
        }
}

static int
load_config(const char *config_path)
{
        Conf conf;
        int len;
#define X(_name, _required, _type, ...) _type _name;
        LIST_OF_TASK_FIELDS()
#undef X

        printf("Loading config file '%s'\n", config_path);
        if (Conf_open(&conf, config_path) != CONF_OK) {
                return 1;
        }

        if (Conf_get_len(conf, &len, "Tasks") != CONF_OK) {
                fprintf(stderr, "load_config: 'Tasks' not found in '%s'\n", config_path);
                Conf_close(conf);
                return 1;
        }

        for (int i = 1; i <= len; i++) {
#define X(_name, _required, _type, _luatype, _default, _copy, ...)                                     \
        do {                                                                                           \
                if (!_required) {                                                                      \
                        _name = _default;                                                              \
                        if (!Conf_get_##_luatype(conf, &_name, "Tasks.%d." #_name, i)) {               \
                                if (_copy != NULL) _name = _copy(_name);                               \
                        }                                                                              \
                } else {                                                                               \
                        if (Conf_get_##_luatype(conf, &_name, "Tasks.%d." #_name, i)) {                \
                                fprintf(stderr, "load_config: Tasks[%d]." #_name " is required\n", i); \
                                Conf_close(conf);                                                      \
                                return 1;                                                              \
                        } else {                                                                       \
                                if (_copy != NULL) _name = _copy(_name);                               \
                        }                                                                              \
                }                                                                                      \
        } while (0);
                LIST_OF_TASK_FIELDS()
#undef X

#define X(n, ...) n,
                task_append(LIST_OF_TASK_FIELDS() 0);
#undef X
        }

        Conf_close(conf);
        return 0;
}

int
main(int argc, char **argv)
{
        const char *version, *verbose, *plain, *remaining, *c_tab_size, *in, *week;
        int ret;

        flag_program(.name = "tui-do", .help = "A terminal todo manager");
        flag_add(&version, "--version", .help = "Show version and exit");
        flag_add(&verbose, "--verbose", .help = "Show more output");
        flag_add(&plain, "--plain", .help = "Use plain output");
        flag_add(&c_tab_size, "--tabsize", .defaults = "4", .help = "Tab size for dumping", .nargs = 1);
        flag_add(&remaining, "--remaining", .help = "Show time left instead of the due date");
        flag_add(&in, "--in", .help = "Only show tasks due in the next N days", .nargs = 1);
        flag_add(&week, "--week", .help = "Only show tasks due until next Monday (exclusive)");

        if (flag_parse(&argc, &argv)) {
                flag_show_help(STDOUT_FILENO);
                flag_free();
                return 1;
        }

        if (version) {
                fprintf(stdout, "%s version %s (%s %s)\n", argv[0], VERSION, __DATE__, __TIME__);
                fprintf(stdout, "Copyright (C) 2026 Hugo Coto\n");
                fprintf(stdout, "This is free software: you are free to change and redistribute it.\n");
                fprintf(stdout, "There is NO WARRANTY, to the extent permitted by law.\n");
                flag_free();
                return 0;
        }

        char *HOME = getenv("HOME");                                                   // to free
        asprintf(&g.default_config, "%s/.config/%s/config.lua", HOME ?: ".", argv[0]); // to free

        g.now       = time(NULL);
        g.tab_size  = atoi(c_tab_size);
        g.verbose   = verbose != NULL;
        g.pretty    = plain == NULL;
        g.remaining = remaining != NULL;

        if (in) {
                g.until     = g.now + atoi(in) * SECS_PER_DAY;
                g.has_until = true;
        }
        if (week) {
                struct tm tm    = *localtime(&g.now);
                int days_ahead  = (1 - tm.tm_wday + 7) % 7; // 1 == Monday
                if (days_ahead == 0) days_ahead = 7;        // today is Monday: use next week's
                tm.tm_mday     += days_ahead;
                tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
                time_t monday   = mktime(&tm);
                g.until         = g.has_until && g.until < monday ? g.until : monday;
                g.has_until     = true;
        }

        ret = load_config(g.default_config);
        for (int i = 1; i < argc; i++) {
                load_config(argv[i]);
        }

        tasks_sort();
        tasks_print();

        task_dump(g.default_config);

        tasks_free();
        flag_free();
        return ret;
}
