# TO-DO terminal task manager

This is the newest version of [TODO](https://github.com/hugoocoto/todo), a many
times used (one per laptop that I own) todo list / reminder for the terminal. 

## About

1. Add your tasks to a lua file
2. Call this program with the desired time interval
3. Get stressed because of the overdued task and the closest the new deadline is.

The lua file is updated on the go, keeping your tasks always updated. You
can add, see, edit and remove tasks. You can load more files and the contents of
the new files are written to the main one.

What I love about this program is that I can call it from the bashrc with
the --quiet flag and the *until monday* option, and only triggers when I have something to do ***now***.

## How it works

Tasks live in a plain Lua file, by default `~/.config/<program-name>/config.lua`,
as a `Tasks` table of `{ name, desc, date }` entries, with `date` built using
Lua's `os.time({ year = ..., month = ..., day = ... })`. Since it's just Lua,
you can always hand-edit this file yourself.

Every run does the same four steps:

1. **Load** — the default config is always read. Any extra `.lua` file paths
   you pass on the command line (`todo other.lua more.lua`) are loaded too,
   and their tasks are merged into the same in-memory list.
2. **Sort & filter** — all tasks are sorted by date, then trimmed down
   according to the flags you passed (`--in`, `--week`, `--overdue`, ...).
3. **Print** — the resulting list is printed to the terminal: colored by
   default (dates in green, overdue ones in red) unless `--plain` is set.
4. **Dump** — the *full* merged list (default config + any extra files) is
   written back out to the default config file, in the same Lua-table format.

That last step is what makes "loading more files" useful: point the program
at a one-off `.lua` file with a couple of tasks in it, and after that single
run those tasks are folded permanently into your main config — no need to
keep passing the extra file on future runs.

## Usage

- `todo`: All the tasks
- `todo --in 7`: The tasks for the next 7 days
- `todo --week`: The tasks for this week (until Monday)
- `todo --overdue`: The tasks that are already due
- `todo --new`: Create a template file, open it in `$EDITOR` and load it
- automatically.
- `todo --edit`: Open the main config in `$EDITOR`, used for modifying or deleting
tasks.

## Build

Just run `make`. It's tested only in linux.

## Limitations

Task dates are stored as a 32-bit `int` (seconds since epoch), so they will
overflow on 2038-01-19 03:14:07 UTC (the [Year 2038 problem](https://en.wikipedia.org/wiki/Year_2038_problem)).
Loading a config with a date past that point will fail with an error instead
of corrupting data, but if this program is still running by then, `date`
needs to become a 64-bit type.
