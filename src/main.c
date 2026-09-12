#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
        if (t.name == NULL || t.name[0] == 0 || t.date == 0) {
                printf("Could not add task: Invalid task");
                return;
        }
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

typedef Da(char *) Command;

void
command_add(Command *c, char *arg)
{
        Da_append(c, arg);
}

void
command_add_many(Command *c, ...)
{
        va_list ap;
        va_start(ap, c);
        while (1) {
                char *arg = va_arg(ap, char *);
                if (arg == NULL) break;
                command_add(c, arg);
        }
        va_end(ap);
}

// ensure that the list is null terminated
#define command_add_many(comm_ptr, ...) command_add_many(comm_ptr, ##__VA_ARGS__, NULL)

// returns pid
int
command_run_async(Command c)
{
        int pid;
        switch ((pid = fork())) {
        case -1:
                fprintf(stderr, "command_run_async: fork fails\n");
                return -1;
        case 0:
                if (c.count > 1) {
                        Da_append(&c, NULL);
                        execvp(c.items[0], c.items);
                        fprintf(stderr, "command_run_async: execvp fails: %s\n", strerror(errno));
                }
                exit(1);
        default:
                return pid;
        }
}

// returns command c exit code
int
command_run_sync(Command c)
{
        int state = 0;
        int pid   = command_run_async(c);

        if (pid < 0) {
                fprintf(stderr, "command_run_sync: invalid command\n");
                return -1;
        }
        assert(pid > 0); // assert that child does not return

        errno = 0;
        if (waitpid(pid, &state, 0) == -1) {
                fprintf(stderr, "command_run_sync: waitpid fails: %s\n", strerror(errno));
                return -1;
        }

        return state;
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

static const char *no_tasks_phrase(time_t seed);

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
        if (g.tasks.count == 0) {
                printf("%s\n", no_tasks_phrase(g.now));
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
        const char *version, *verbose, *plain, *remaining, *overdue, *c_tab_size, *in, *week, *new, *edit;
        bool list_tasks = true; // list tasks by default
        int ret;

        flag_program(.name = "tui-do", .help = "A terminal todo manager");
        flag_add(&version, "--version", .help = "Show version and exit");
        flag_add(&verbose, "--verbose", .help = "Show more output");
        flag_add(&new, "--new", .help = "Create a new task");
        flag_add(&edit, "--edit", .help = "Edit tasks");
        flag_add(&plain, "--plain", .help = "Use plain output");
        flag_add(&c_tab_size, "--tabsize", .defaults = "4", .help = "Tab size for dumping", .nargs = 1);
        flag_add(&remaining, "--remaining", .help = "Show time left instead of the due date");
        flag_add(&overdue, "--overdue", .help = "Only show tasks due before now");
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
        if (overdue) {
                g.until     = g.has_until && g.until < g.now ? g.until : g.now;
                g.has_until = true;
        }
        if (week) {
                struct tm tm   = *localtime(&g.now);
                int days_ahead = (1 - tm.tm_wday + 7) % 7; // 1 == Monday
                if (days_ahead == 0) days_ahead = 7;       // today is Monday: use next week's
                tm.tm_mday += days_ahead;
                tm.tm_hour = tm.tm_min = tm.tm_sec = 0;

                time_t monday = mktime(&tm);
                g.until       = g.has_until && g.until < monday ? g.until : monday;
                g.has_until   = true;
        }

        if (new) {
                list_tasks = true; // list task after the new one is created.
                                   // Change to false to skip printing tasks.
                char *editor = getenv("EDITOR");
                char tmp[]   = "/tmp/todo/XXXXXX.lua";

                char *path = dirname(strdup(tmp));
                mkdirp(path, 755);
                free(path);
                mkstemps(tmp, strlen(".lua"));

                FILE *f = fopen(tmp, "w");
                if (!f) {
                        fprintf(stderr, "File %s can not be written\n", tmp);
                } else {
                        struct tm *tm = localtime(&g.now);
                        fprintf(f, "Tasks = {\n");
                        fprintf(f, TAB "{\n", TAB_C);
                        fprintf(f, TAB TAB "name = \"\",\n", TAB_C, TAB_C);
                        fprintf(f, TAB TAB "desc = \"\",\n", TAB_C, TAB_C);
                        fprintf(f, TAB TAB "date = os.time({ year = %d, month = %d, day = %d, hour = %d, }),\n", TAB_C, TAB_C, tm->tm_year + 1900, tm->tm_mon, tm->tm_mday, tm->tm_hour);
                        fprintf(f, TAB "},\n", TAB_C);
                        fprintf(f, "}\n");
                        fflush(f);
                        fclose(f);
                }

                if (!editor) {
                        fprintf(stderr, "env var EDITOR not set:\n");
                        fprintf(stderr, "Edit %s by hand, then run `%s %s`\n", tmp, argv[0], tmp);
                } else {
                        Command c = { 0 };
                        int status;
                        command_add_many(&c, editor, tmp);
                        status = command_run_sync(c);
                        if (status == 0) load_config(tmp);
                }
        }

        if (edit) {
                list_tasks = true; // list task after the new one is created.
                                   // Change to false to skip printing tasks.
                char *editor = getenv("EDITOR");

                if (!editor) {
                        fprintf(stderr, "env var EDITOR not set:\n");
                        fprintf(stderr, "Edit %s by hand, then run `%s %s`\n", g.default_config, argv[0], g.default_config);
                } else {
                        Command c = { 0 };
                        int status;
                        command_add_many(&c, editor, g.default_config);
                        status = command_run_sync(c);
                        if (status) {
                                fprintf(stderr, "Could not open file in editor:\n");
                                fprintf(stderr, "Edit %s by hand, then run `%s %s`\n", g.default_config, argv[0], g.default_config);
                        }
                }
        }


        ret = load_config(g.default_config);
        for (int i = 1; i < argc; i++) {
                load_config(argv[i]);
        }

        if (list_tasks) {
                tasks_sort();
                tasks_print();
        }

        task_dump(g.default_config);

        tasks_free();
        flag_free();
        return ret;
}

static const char *
no_tasks_phrase(time_t seed)
{
        static const char *const list[] = {
                "No tasks",
                "Nothing to do",
                "Nothing pending",
                "Task list empty",
                "All tasks completed",
                "Nothing scheduled",
                "No pending items",
                "You're all caught up",
                "Nothing on the list",
                "Zero tasks remaining",

                "Nothing to do. Suspicious.",
                "Your to-do list called. It has nothing to say.",
                "Inbox zero. Task zero. Hero zero.",
                "Error 404: tasks not found.",
                "The list is empty. Don't look so relieved.",
                "Plot twist: you did everything.",
                "Congratulations, you have defeated the to-do list.",
                "This is not a drill. You're actually free.",
                "Achievement unlocked: Empty Inbox.",
                "The tasks fled. You win.",
                "Nothing here but tumbleweeds.",
                "Task list emptier than my coffee cup.",
                "404: Responsibilities not found.",
                "Even your to-do list is impressed.",
                "Somewhere, a task is crying because it wasn't created.",
                "This space intentionally left blank (by you, nicely done).",
                "Your future self says thanks.",
                "Nothing to do. Go bother someone else's to-do list.",
                "The robots checked twice. Still nothing.",
                "The list is so empty it echoes.",

                "Small steps, repeated, become big things.",
                "Rest is productive too.",
                "An empty list is just tomorrow's blank page.",
                "Discipline got you here. Enjoy it.",
                "No tasks left. What will you create next?",
                "You showed up, and it worked.",
                "Consistency beats intensity.",
                "This is what winning looks like.",
                "Clear list, clear mind, keep going.",
                "You did the work. Now breathe.",
                "Momentum starts with moments like this.",
                "Every finished task was once a blank line too.",
                "You're exactly where discipline gets you.",
                "Progress doesn't always look loud.",
                "The work paid off. Look at this list.",
                "Keep showing up. It compounds.",
                "Nothing left because you left nothing undone.",
                "This silence is earned.",
                "You built this empty list one task at a time.",
                "Proof that you finish what you start.",
                "The best to-do list is the one you cleared.",
                "You're not behind. You're done.",
                "Today's effort, tomorrow's ease.",
                "Small wins add up to this.",
                "You earned this quiet.",

                "Empty list, clear mind.",
                "You've earned this silence.",
                "Nothing to chase right now. Just be.",
                "Stillness is allowed.",
                "The mind rests when the list does.",
                "Breathe. There's nothing pulling at you.",
                "An empty list is a quiet room.",
                "Nothing urgent. Nothing pending. Just now.",
                "Peace looks like this list.",
                "No tasks. No noise.",
                "This is what enough feels like.",
                "Sit with the quiet for a moment.",
                "The list is empty. So is the worry.",
                "Nothing to do but exist for a bit.",
                "A clear list makes room for a clear thought.",

                "Nothing scheduled. Go build something anyway.",
                "exit code 0: success, nothing to run.",
                "git status: nothing to commit, working tree clean.",
                "Compiled cleanly. No warnings. No tasks.",
                "This function returned early: nothing to do.",
                "Task queue: empty. Worker: idle. You: free.",
                "while (tasks) { } never even looped.",
                "0 tasks found. 0 bugs too, hopefully.",
                "The backlog's backlog is itself empty.",
                "NULL tasks. Not a bug, a feature.",
                "Stack's empty. Pop nothing. Relax.",
                "No pending PRs on your to-do list.",
                "The cron job ran and found nothing to do.",
                "Build succeeded. Task list: 0 errors, 0 tasks.",
                "Your to-do list just returned void.",

                "The tasks went on strike. You won.",
                "This list has seen better days. Kidding, this is its best day.",
                "Nothing to do. The universe owes you one.",
                "Somehow, everything got done. Suspicious, but we'll take it.",
                "You've out-organized your own chaos.",
                "Tasks: 0. Ego: slightly bigger.",
                "This is the calm after doing the storm.",
                "Your productivity ghosted its own to-do list.",
                "The list looked back at you and shrugged. Empty.",
                "Somewhere a task manager is out of a job today.",
                "Nothing left to procrastinate on.",
                "You ran out of things to avoid doing.",
                "The empty list salutes you.",
                "Go outside. The list can't stop you now.",
                "Task list closed for lack of business.",
        };
        size_t len = sizeof list / sizeof list[0];
        srand(seed);
        return list[rand() % len];
}
