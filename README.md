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

## Limitations

Task dates are stored as a 32-bit `int` (seconds since epoch), so they will
overflow on 2038-01-19 03:14:07 UTC (the [Year 2038 problem](https://en.wikipedia.org/wiki/Year_2038_problem)).
Loading a config with a date past that point will fail with an error instead
of corrupting data, but if this program is still running by then, `date`
needs to become a 64-bit type.
