# `port/debug/` — console scripts

Files the in-game debug console's `exec` command reads. See
[`docs/DEBUG_SUITE.md`](../../docs/DEBUG_SUITE.md) for the full reference.

- `exec <name>` runs `<name>.cfg` from this directory. One console command
  per line; `#` and `//` start a comment; blank lines are ignored.
- **`autoexec.cfg`, if it exists, runs automatically at startup.** It is
  deliberately not committed — it is your own working state, and a
  half-finished repro belonging to one person should not run in everyone
  else's session.

The point of a `.cfg` is that a bug repro becomes a file. Instead of
"load azra, walk to the second corridor, open the door, note that…",
you write the setup down once and `exec` it every launch.

`example.cfg` here is a working sample; copy it to `autoexec.cfg` and edit.
