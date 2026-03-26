# `tic80ctl` Shim

`tic80ctl` is now a small C CLI plus a hidden local supervisor over the existing TIC-80 MCP server.

It does **not** define new gameplay or screenshot semantics. Instead, it reuses the existing MCP contracts:

- `run_command`
- `capture_screenshot`
- `run_playtest_episode`
- editor authoring tools such as `sfx_*`, `music_*`, `sprite_*`, and `map_*`

## Design Intent

The shim exists to provide a command-oriented interface without abandoning MCP as the semantic source of truth.

That means:

- MCP remains the core agent contract
- `tic80ctl` is a transport and ergonomics layer
- CLI behavior should mirror MCP behavior rather than drift from it

## Session Model

V1 uses one long-lived default session owned by a local supervisor process.

Commands:

- `tic80ctl start`
- `tic80ctl status`
- `tic80ctl stop`

The session persists:

- supervisor PID
- loopback TCP port + session token
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

Editor domains:

- `tic80ctl sfx <wavetable|volume|wave|arpeggio|pitch|panning|speed|loop> ...`
- `tic80ctl music <track|frame|row|rows> ...`
- `tic80ctl sprite <tile|region|palette> ...`
- `tic80ctl map <rect|chunk> ...`

The editor surface uses merged verbs by arity:

- selector-only calls are getters
- selector plus compact payload calls are setters
- `--args-json '<raw MCP arguments>'` forces the setter path and bypasses compact payload parsing

Compact payload conventions:

- SFX wavetable: 32 hex nybbles
- SFX volume/wave/pitch: `tick:value,...` keyframes expanded to 30 steps
- SFX arpeggio: CSV semitone list, repeated to fill 30 steps
- SFX panning: `left,right` booleans
- SFX loop: `start:size`
- music track: `tempo,speed,rows`
- music frame: `p0,p1,p2,p3`
- music row: `note:sfx:cmd`
- music rows setter: `row:note:sfx:cmd,...`
- sprite tile: `row0,row1,...,row7` where each row is 8 hex digits
- sprite region: semicolon-separated tile payloads in row-major order
- sprite palette: 16 comma-separated `RRGGBB` values
- map chunk: row-major CSV tile ids
- map rect setter: single fill tile id

## Runtime Launch

`tic80ctl start` starts an empty session only. It must not accept a cart path. Carts are loaded afterward with:

```sh
tic80ctl load <cart_path>
```

The supervisor launches TIC-80 with the headless-safe MCP shape:

```sh
xvfb-run --auto-servernum ./bin/tic80 --skip --soft --mcp --fs "$PWD"
```

This preserves the existing rule that `--mcp` is transport-only while moving durability, liveness tracking, and request serialization into a process that is suited to supervision.

If needed, the full launch command can be overridden with `TIC80CTL_LAUNCH_COMMAND`.
When set, `tic80ctl` executes that exact command via the shell from the session working directory instead of using the default launch shape.

## Supervisor Contract

The hidden server mode owns:

- the real TIC-80 MCP child process
- the child stdio pipes
- MCP initialize/probe during startup
- serialized MCP request forwarding for later CLI invocations
- session health reporting based on real child usability rather than shell wrapper guesses

`tic80ctl start` only succeeds after:

- the supervisor is running
- TIC-80 launched
- MCP initialize succeeded
- a follow-up probe succeeded
- the child still appears alive after initialization

## Output Style

Default output should be human-readable text.

`--json` is supported for machine-readable wrapping, but the CLI should still expose MCP result semantics directly:

- MCP `isError:true` -> nonzero shell exit
- MCP text content -> printed output
- editor getters without `--json` print the MCP `structuredContent` object directly for quick inspection

## Required Coverage

At minimum:

- start/status/stop
- `cmd`
- `load` / `run` / `eval`
- `screenshot`
- `playtest`
- editor getters and setters across `sfx`, `music`, `sprite`, and `map`
- `--args-json` setter passthrough for editor commands
- propagation of MCP errors through shell exit status
