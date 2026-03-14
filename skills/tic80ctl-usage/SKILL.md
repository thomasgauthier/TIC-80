---
name: tic80ctl-usage
description: Operate TIC-80 through the `tic80ctl` CLI. Use when Codex should manage a long-lived TIC-80 session from the shell, send console commands, capture screenshots, run playtest episodes, and iteratively build or debug TIC-80 games through the command-oriented interface.
---

# `tic80ctl` Usage

Use `tic80ctl` as the command-oriented shell interface for TIC-80.

Treat it as:

- a long-lived TIC-80 session manager
- a command-line way to load, run, eval, screenshot, and playtest carts
- a practical agent loop for building and refining TIC-80 games

`tic80ctl` does **not** invent its own gameplay semantics. It is a small command surface over TIC-80’s existing console, screenshot, and playtest capabilities.

This skill also depends on local references under `reference/`.

Curated references:

- `reference/workflow_rationale.md`
- `reference/scripted_playtest_guide.md`
  - scripted playtest episode guide
- `reference/tic80_practical_workflow.md`
  - console, reload, external-editor, and multi-file workflow notes
- `reference/tic80_lua_api_quickref.md`
  - high-frequency TIC-80 Lua API reminders for normal agent work
- `reference/official_tic80_learn_reference.md`
  - official general TIC-80 reference sourced from `https://tic80.com/learn`

Copied source references from `./tic80_general_documentation` are also available for targeted lookup:

- `reference/source_tips_for_newcomers.md`
- `reference/source_console.md`
- `reference/source_reload.md`
- `reference/source_external_editor.md`
- `reference/source_splitting_projects.md`
- `reference/source_require_workflow.md`
- `reference/source_api.md`
- `reference/source_api_cheatsheet.md`

Treat those as first-class guidance when the task involves iterative playtesting, evidence-driven debugging, or designing episode-based validation loops.

Read `reference/tic80_practical_workflow.md` when you need concrete TIC-80 console, reload, or external-editor workflow details rather than high-level iteration advice.
Read `reference/tic80_lua_api_quickref.md` when you need fast reminders for the common Lua-side APIs used during gameplay iteration, probing, and debugging.
Read `reference/official_tic80_learn_reference.md` when you want the official broad TIC-80 overview, specs, console commands, RAM layout, and built-in API catalog in one place.
Use the copied source references when you need the original TIC-80 wording, examples, or caveats behind the curated guidance.

## Core Design Philosophy

Default to this loop:

1. start or reuse one TIC-80 session
2. set up the cart
3. run one bounded experiment
4. inspect the evidence
5. revise
6. repeat

Do **not** default to many tiny ad hoc shell actions when one episode or one targeted command can answer the question.

The best abstraction is usually:

- “run one bounded experiment and inspect the resulting evidence”

not:

- “poke the runtime repeatedly until you think you understand it”

## Game Development Approach

Build small, finishable games instead of vague toy prototypes.

Treat TIC-80 game work as four linked problems:

1. pick a game that fits the scope
2. encode the game as clear state and deterministic rules
3. make the moment-to-moment play readable and satisfying
4. verify it by actually running and playtesting it, not by inspection alone

The closed loop is not just for debugging. It is the normal way to design, validate, and tighten the game.

## Default Scope

Start with a vertical slice that can be completed end to end:

- one player verb set
- one core loop
- one failure state
- one win condition
- one short route or encounter sequence

Expand only after that slice is playable.

Good TIC-80-sized concepts:

- top-down action rooms
- arcade survival loops
- lane-based dodge/collect games
- simple platformers with a few authored jumps
- micro stealth or chase games
- score-attack puzzle/action hybrids

Avoid starting with:

- procedural worlds
- deep inventory systems
- large dialogue trees
- heavy physics simulation
- content-heavy RPG structures

## Planning Pattern

Before writing much code, define these concretely:

- fantasy: what the player is doing in one sentence
- verbs: move, jump, dash, shoot, interact, collect
- loop: what repeats every few seconds
- hazards: what creates pressure
- progression: what must happen to reach the end
- acceptance check: what a successful playtest must prove

Prefer room-by-room or phase-by-phase progression over sprawling maps.
The best `tic80ctl` playtest loops come from routes that can be named, scripted, and checked as explicit segments.

## Design Strategies That Work Well

Use these patterns by default:

- build around a route the player can describe out loud
- gate progression with readable obstacles
- add one signature move only if it clearly improves the game
- theme each room or phase so progress feels legible
- let level data do most of the work

Examples of good progression gates:

- patrol timing
- hazards
- locked exits
- key pickups
- narrow traversal checks

Good route descriptions sound like:

- collect relic, cross lane, climb, cross top lane, drop, exit
- start room, grab key, unlock gate, dodge patrol, reach door

If the route is hard to describe, it is often also hard to test and hard to tune.

## Feel And Readability

TIC-80 games benefit more from clarity than from complexity.

Priorities:

- immediate input response
- strong contrast between safe and dangerous space
- visible player position at all times
- obvious collectibles, goals, and exits
- short HUD text with controls or objective
- restart path that is fast and reliable

Add juice selectively:

- small camera shake or flash on hit
- palette contrast between zones
- tiny motion or blinking on goals
- brief particles or rings for pickups
- sound cues for success, danger, and failure

Do not add polish before the route is proven playable.

## Mental Model

Separate these layers:

1. session lifecycle
2. cart lifecycle
3. console command execution
4. playtest episode execution

Most mistakes come from mixing them.

## Session Lifecycle

V1 uses one long-lived default session.

Core commands:

- `tic80ctl start`
- `tic80ctl status`
- `tic80ctl stop`

The session persists enough metadata to support reconnect and inspection:

- PID
- cwd
- stdout log path
- stderr log path
- launch mode

### `start`

Use `start` to create the default session.

Examples:

```sh
tic80ctl start
```

Behavior:

- if no session exists, starts a durable TIC-80 session
- `start` is session-only; load carts afterward with `load` or `cmd "load ..."`
- uses the current working directory as the TIC filesystem root
- uses a headless-safe launch path so the session survives after `start` exits

Practical rule:

- start the session from the project root you want TIC-80 to treat as its filesystem root
- keep carts, screenshots, and any required Lua modules under that root
- if paths behave strangely, inspect the effective root first

If a session is already running, `start` should report that instead of spawning duplicates.

### `status`

Use `status` to see whether the default session is alive.

Examples:

```sh
tic80ctl status
tic80ctl --json status
tic80ctl status --json
```

Human output is for readability. JSON output is for machine use.

### `stop`

Use `stop` to end the default session.

Examples:

```sh
tic80ctl stop
tic80ctl stop --json
```

If no session is running, expect a clear error or stopped=false JSON response depending on mode.

## Cart Lifecycle

Inside the running session, use TIC-80 commands with these meanings:

- `load cart.lua`
  - select the cart/project contents
- `run`
  - start the VM for the currently loaded cart
- `eval ...`
  - execute inside the running cart VM

Do not assume `eval` creates the runtime by itself.
If runtime probing behaves strangely, check the practical workflow reference first; the most common cause is that the cart has not been run yet.

## Main Command Surface

The main passthrough is:

```sh
tic80ctl cmd "<tic80 command>"
```

Use that for anything the fantasy editor console already supports.

Convenience aliases:

- `tic80ctl load <cart_path>`
- `tic80ctl run`
- `tic80ctl eval "<expr>"`
- `tic80ctl screenshot [path]`
- `tic80ctl playtest --script-file <file> ...`

These aliases are only convenience wrappers around the same underlying `tic80ctl` session commands.

## Output Modes

Default:

- human-readable text

Optional:

- `--json`

`--json` may appear globally or immediately after the subcommand:

```sh
tic80ctl --json status
tic80ctl status --json
tic80ctl load --json main.lua
```

Important rule:

- if the underlying command result is an error, `tic80ctl` exits nonzero

That means shell exit codes are meaningful and should be checked in automation.

## `cmd`

Use `cmd` for raw TIC-80 console commands.

Examples:

```sh
tic80ctl cmd "help commands"
tic80ctl cmd "load main.lua"
tic80ctl cmd "run"
tic80ctl cmd "eval trace(type(TIC))"
```

Use `cmd` when:

- you want console parity
- you need a command without a dedicated alias
- you are debugging or exploring interactively

Prefer aliases for the common cases because they are shorter and clearer.

## `load`

Alias for:

```sh
tic80ctl cmd "load <cart_path>"
```

Example:

```sh
tic80ctl load carts/main.lua
```

Successful output should resemble TIC-80 console output:

- cart loaded
- reminder to use `run`

## `run`

Alias for:

```sh
tic80ctl cmd "run"
```

Use it to start the currently loaded cart runtime.

Important behavior:

- first-frame runtime errors can surface here
- if the cart fails synchronously during the first run-mode frame, `tic80ctl run` should print the cart/runtime error and exit nonzero

Treat that as a real cart/runtime issue, not a CLI transport problem.

## `eval`

Alias for:

```sh
tic80ctl cmd "eval <expr>"
```

Use it for short runtime probes and mutations.

Good probes:

```sh
tic80ctl eval "trace(type(TIC))"
tic80ctl eval "trace(frame)"
tic80ctl eval "trace(player_x)"
tic80ctl eval "some_flag = true"
```

Prefer short payloads.

Use `trace(...)` for machine-readable observations.

Do not assume `eval` can initialize the runtime by itself; call `run` first.

When debugging a deep-in-route bug, combine:

- reload-friendly state
- `resume reload`
- `trace(...)`
- short `eval` probes

That lets you patch code and continue from the current situation instead of replaying the whole route from the beginning.

## `screenshot`

Examples:

```sh
tic80ctl screenshot
tic80ctl screenshot shots/frame.png
tic80ctl screenshot --json shots/frame.png
```

Important path rule:

- paths are interpreted relative to the active TIC filesystem root (`./`)
- do not use absolute host paths

Important behavior:

- missing relative subdirectories produce a specific error

Example:

```sh
tic80ctl screenshot shots/frame.png
```

If `shots/` does not exist, expect an error like:

- `relative screenshot directory does not exist: shots`

Use screenshots for:

- targeted visual confirmation
- before/after checks
- one-off inspection outside episode flow

Do not manually build long frame sequences with repeated `screenshot` calls when `playtest` is available.

## Text Cart Workflow

For larger projects, prefer a real text-cart workflow over treating the cart as a tiny in-editor sketch forever.

Good default:

1. start `tic80ctl` from the repo or project root
2. load a text cart such as `game.lua`
3. edit code in an external editor
4. use the live `tic80ctl` session for run, eval, screenshot, and playtest
5. return to TIC-80 resource editors only when you need sprites, map, sfx, or music edits

Important habits:

- keep code changes at the top of text carts
- avoid manually editing resource data blocks unless you mean to
- avoid making unsaved changes in TIC resource editors and in an external editor at the same time
- use `CTRL+O` inside TIC when you need quick structure navigation in a larger cart

If the project is growing, read `reference/tic80_practical_workflow.md` before improvising a custom setup.

## `playtest`

Use it for scripted gameplay/control episodes.

Required:

- `--script-file <file>`

Optional:

- `--timeout <seconds>`
- `--input-overlay`
- `--no-input-overlay`

Examples:

```sh
tic80ctl playtest --script-file episode.lua
tic80ctl playtest --script-file episode.lua --timeout 5 --no-input-overlay
```

The command:

- runs the episode script in a separate episode-owned Lua state
- drives the loaded cart through the existing runtime
- captures one PNG per advanced frame
- returns artifact paths and summary text

Typical output includes:

- `status=...`
- `message=...`
- `artifact_path=./playtest/episode_n`
- `frames=...`

## Playtesting Is Part Of The Work

Never treat TIC-80 game work as complete until it has been run.

Use the command surface with this priority:

- use `load` and `run` to set up the cart
- use `playtest` for traversal, combat, progression, and route validation
- use `screenshot` only for targeted visual confirmation
- use `eval` only for short runtime probes or toggles

Prefer scripted episodes over ad hoc manual probing when validating gameplay.

The default loop should be:

1. plan the route or experiment
2. encode it as a compact episode script
3. run it once
4. inspect the returned artifacts
5. revise either the game or the script
6. run again

Do not model the task as “press buttons frame by frame across many unrelated shell turns” unless you are debugging a very narrow one-off interaction.

### Playtest Pattern

Create scripts that model a real route:

- start game
- hold directional input for known stretches
- trigger action buttons at deliberate moments
- log each segment with a short label
- end with an explicit success or failure message

This pattern is robust:

```lua
function hold(input, frames, label)
  if label then log(label) end
  for i=1,frames do
    set_input(input)
    frameadvance()
  end
  set_input({})
end
```

Use named route segments in the log so failures are localized quickly.

Examples:

- `start game`
- `cross first lane`
- `collect key`
- `climb to upper route`
- `dash through final guard`
- `reach exit`

## Playtest Script API

The episode script surface is intentionally small.

Use only:

- `frameadvance()`
- `set_input(input_table)`
- `set_input(player_num, input_table)`
- `log(text)`
- `end_episode(status, message)`

### `frameadvance()`

Advance exactly one gameplay frame.

This:

- consumes the currently staged one-frame input
- advances the cart one frame
- captures the resulting frame artifact
- increments the frame count

### `set_input(input_table)`

Target player 1 by default.

Example:

```lua
set_input({right=true, a=true})
frameadvance()
```

### `set_input(player_num, input_table)`

Target another player explicitly.

Example:

```lua
set_input(2, {left=true, b=true})
frameadvance()
```

### Valid Button Names

- `up`
- `down`
- `left`
- `right`
- `a`
- `b`
- `x`
- `y`

### Input Semantics

Remember:

- unspecified buttons default to `false`
- input is one-frame-only
- `set_input(...)` prepares the next frame
- `frameadvance()` consumes that prepared input
- input clears automatically after the frame

### `log(text)`

Write script-authored route annotations to `log.txt`.

Use it for:

- route segment names
- checkpoints
- experiment labels

### `end_episode(status, message)`

Terminate the episode deliberately.

Examples:

```lua
end_episode("done", "baseline")
end_episode("success", "reached exit")
end_episode("failure", "player died")
```

## `DEBUG_MODE` During Playtest

For Lua carts only, `playtest` enables cart-side `DEBUG_MODE=true` for the duration of the episode and restores `DEBUG_MODE=nil` afterward.

This means cart code can include debug-only rendering or logging like:

```lua
if DEBUG_MODE then
  rectb(player.x-2, player.y-2, 20, 20, 2)
  trace("debug player_x="..player.x)
end
```

Important points:

- `DEBUG_MODE` exists in the cart runtime, not the episode script runtime
- it is temporary
- it is ideal for hitboxes, room ids, patrol paths, collision probes, camera zones, and debug-only `trace(...)`
- later standalone screenshots after the episode should no longer show those debug visuals

## Iteration Loop

Follow this cycle:

1. run the current cart
2. playtest a realistic route
3. inspect `log.txt`, `console.txt`, and a few targeted frames
4. fix level layout first when traversal is broken
5. change physics only if the intended design clearly demands it
6. rerun the same route
7. keep a stable passing script and one or more proof artifacts

This ordering matters. Good TIC-80 iteration comes from fixing progression with evidence, not from making random feel tweaks and hoping the route improves.

If a run matters, preserve it under durable names such as:

- `playtest/full_clear_script.lua`
- `playtest/full_clear_log.txt`
- `playtest/full_clear_console.txt`
- `playtest/full_clear_win.png`

Treat a passing route as part of the deliverable, not just a temporary check.

## Reporting Pattern

When summarizing progress, anchor it in route evidence.

Good pattern:

- state the current finding plainly
- choose one next probe
- distinguish execution error from design error
- preserve passing evidence once the route succeeds

Examples of useful summaries:

- the first realistic route failed before the next progression step, so inspect the key frames around that segment before changing movement code
- the route now passes, so preserve the script and final proof artifacts under stable filenames
- the issue is level geometry rather than timing sensitivity, so fix layout before touching jump tuning

## Artifact Layout

Expect:

```text
playtest/
  episode_1/
    script.lua
    log.txt
    console.txt
    screenshots/
      000001.png
      000002.png
      ...
```

Interpretation:

- `script.lua`
  - exact episode script that ran
- `log.txt`
  - script-authored `log(...)` output
- `console.txt`
  - cart-side `trace(...)` output during the episode
- `screenshots/*.png`
  - one screenshot per advanced frame

The PNG filename is the frame number.

## Building And Improving TIC-80 Games With `tic80ctl`

Use `tic80ctl` not only for inspection, but as the default game-iteration loop.

Think of TIC-80 game work as four linked problems:

1. choose a finishable game scope
2. encode clear deterministic rules
3. make moment-to-moment play readable
4. prove it by actually running and playtesting

### Good Scope Defaults

Prefer:

- top-down action rooms
- arcade survival loops
- lane-based dodge/collect games
- simple platformers
- micro stealth or chase games

Avoid starting with:

- procedural worlds
- deep inventory systems
- large dialogue trees
- heavy physics simulation
- content-heavy RPG structures

### Default Iteration Loop

Use this sequence:

1. `tic80ctl start`
2. `tic80ctl load <cart>`
3. `tic80ctl run` if needed
4. run one `playtest` episode
5. inspect `log.txt`, `console.txt`, and key frame PNGs
6. revise the cart
7. rerun the same route

Do not rely on code inspection alone for gameplay issues.

### Good Route Pattern

Encode a route the player can describe out loud:

- start game
- cross first hazard
- collect key
- climb to upper route
- reach exit

This makes both design and debugging easier.

### Example Hold Helper

```lua
local function hold(input, frames, label)
  if label then log(label) end
  for i=1,frames do
    set_input(input)
    frameadvance()
  end
end
```

### Example Baseline

```lua
log("baseline")
frameadvance()
end_episode("done", "baseline")
```

### Example Movement Segment

```lua
log("move right")
for i=1,24 do
  set_input({right=true})
  frameadvance()
end
end_episode("done", "right movement complete")
```

### Example Debug Validation

If the cart contains:

```lua
if DEBUG_MODE then
  rectb(x-2, y-2, 20, 20, 2)
  trace("debug on")
end
```

then:

```lua
log("debug baseline")
frameadvance()
end_episode("done", "capture debug frame")
```

should produce:

- debug-only visuals in the episode frame
- matching `console.txt` lines

## Failure Interpretation

Use these heuristics:

- `tic80ctl: no active session`
  - run `tic80ctl start`
- `unknown command: ...`
  - the console command is invalid, not the shim
- synchronous error from `tic80ctl run`
  - the cart failed during its first run-mode frame
- `path must be relative to the TIC filesystem root`
  - invalid screenshot path
- `relative screenshot directory does not exist: <dir>`
  - target subdirectory missing
- `function` from `tic80ctl eval "trace(type(TIC))"`
  - runtime exists
- empty `console.txt`
  - the cart did not call `trace(...)` during the episode

## Reliable Habits

Use these consistently:

- keep one session alive while iterating
- use `load`, `run`, and `eval` for setup and probing
- use `playtest` for multi-frame questions
- use cart-side `DEBUG_MODE` for playtest-only instrumentation
- inspect returned artifact paths instead of guessing
- check shell exit codes in automation
- use `--json` when another tool needs to parse the result

## Practical Decision Rule

Use this rule:

- if the task is “send one TIC-80 console command,” use `cmd` or an alias
- if the task is “get one current frame,” use `screenshot`
- if the task is “execute a plan over frames and inspect the artifact,” use `playtest`

That is the main split.
