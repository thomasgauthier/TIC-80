# `tic80ctl` Shim

`tic80ctl` is a POSIX-shell CLI shim over the existing TIC-80 MCP server.

It does **not** define new gameplay or screenshot semantics. Instead, it reuses the existing MCP contracts:

- `run_command`
- `capture_screenshot`
- `run_playtest_episode`

## Design Intent

The shim exists to provide a command-oriented interface without abandoning MCP as the semantic source of truth.

That means:

- MCP remains the core agent contract
- `tic80ctl` is a transport and ergonomics layer
- CLI behavior should mirror MCP behavior rather than drift from it

## Session Model

V1 uses one long-lived default session.

Commands:

- `tic80ctl start [cart_path]`
- `tic80ctl status`
- `tic80ctl stop`

The session persists:

- PID
- cwd
- stdout/stderr log paths

## Command Surface

Main passthrough:

- `tic80ctl cmd "<tic80 command>"`

Convenience aliases:

- `tic80ctl load <cart_path>`
- `tic80ctl run`
- `tic80ctl eval "<expr>"`
- `tic80ctl screenshot [path]`
- `tic80ctl playtest --script-file <file> ...`

## Runtime Launch

`tic80ctl start` should prefer a plain graphical launch when a display is already available, and otherwise fall back to a headless-safe launch using:

```sh
xvfb-run --auto-servernum ./bin/tic80 --skip --soft --mcp --fs .
```

This preserves the existing rule that `--mcp` is transport-only, while still giving the CLI a reliable terminal/CI story.

## Output Style

Default output should be human-readable text.

`--json` is supported for machine-readable wrapping, but the CLI should still expose MCP result semantics directly:

- MCP `isError:true` -> nonzero shell exit
- MCP text content -> printed output

## Required Coverage

At minimum:

- start/status/stop
- `cmd`
- `load` / `run` / `eval`
- `screenshot`
- `playtest`
- propagation of MCP errors through shell exit status
