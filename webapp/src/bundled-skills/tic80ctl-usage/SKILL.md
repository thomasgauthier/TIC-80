---
name: tic80ctl-usage
description: Use `tic80ctl` to start TIC-80, load and run carts, inspect a live game, edit cartridge content from the shell, and run scripted playtests while building games.
---

# `tic80ctl` Usage

Use `tic80ctl` when you want to work on a TIC-80 game from the shell.

It is useful for:

- starting and stopping a TIC-80 session
- loading and running a cart
- sending TIC-80 console commands
- evaluating small Lua probes in the running cart
- capturing screenshots
- running scripted playtests
- editing SFX, music, sprites, palette data, and map data

If you need the full command list or exact flags, run:

```sh
tic80ctl --help
```

## Scope And Priorities

This skill teaches TIC-80 shell workflow and runtime verification.

Use it to:

- choose the right bounded `tic80ctl` step
- confirm cart/runtime behavior with `run`, `eval`, `screenshot`, and `playtest`
- avoid common TIC-80 workflow mistakes

Do not use this skill as permission to:

- invent a new engine structure
- broadly scan repo docs when one named reference file is enough
- read whole reference files when one relevant section is enough for the next step
- debug the environment before checking the next direct runtime step
- replace normal code-writing tools with shell redirection

For ordinary gameplay carts, default to:

- one script cart
- one global `function TIC()` frame callback
- one bounded gameplay loop proved by runtime verification

## Structural Guardrails

Before you spend time on runtime probing, make sure the cart shape is actually TIC-80 Lua.

Good defaults:

- use exactly one global `function TIC()` callback for the frame loop
- keep persistent gameplay state outside `TIC()`
- use real TIC-80 APIs like `cls`, `print`, `rect`, `circ`, `spr`, `btn`, and `btnp`
- assume the ordinary TIC-80 screen unless a local reference says otherwise: `240x136`, 16 colors, normal color ids `0..15`
- for normal movement input, use gamepad-style `btn/btnp`
- directional ids are: `btn(0)` for up, `btn(1)` for down, `btn(2)` for left, `btn(3)` for right

Do not treat these as acceptable TIC-80 structure:

- `init()`, `update()`, `draw()`, `input()`, or `main()` as the primary game loop
- `love.*` callbacks or another engine family
- `function TIC()` plus leftover `load()` / `update()` / `draw()` wrappers around the real logic
- bare `TIC()` or brace-style callback syntax instead of normal Lua `function TIC() ... end`

Known fake or high-risk API drift to reject quickly:

- `input.pressed`, `input.a`, `input.p1`
- `tic.mode`, `tic.frame`
- `TIC_RUN`, `TIC_STOP`
- `TIC.w`, `TIC.h`, `TIC.font`, `TIC.cameraX`, `TIC.cameraY`, `TIC.sfx`
- `kdbp`, `kdb`, `kbds`, `kpressed`
- `printb`, `drawtext`, `text`, `set`
- `fill_rect`, `set_color`, `first_frame`
- `rand(` when it is being used as guessed TIC-80 API rather than deliberate Lua code

If you are unsure whether a helper exists:

- do not invent a replacement helper
- simplify the cart to a smaller version that only uses APIs already grounded in the local references
- prefer one tiny working loop over a larger guessed abstraction

If you already got one exact correction for structural drift:

- do not produce a near-variant of the same wrong family
- leave the blocked family entirely and collapse to the smallest grounded cart that can still be playtested

Input anti-patterns to reject:

- string or symbolic arguments to `btn(...)` / `btnp(...)`
- keyboard-style high button ids such as `btnp(16)`
- comparisons like `btn(...) == 3`
- comparisons like `key(...) == "left"` or similar string-direction checks

If non-directional buttons are still uncertain after reading the API reference:

- do not guess `btnp(4) or similar`
- simplify the game so start, restart, and core interaction can be proven with grounded directional input only

Text and drawing anti-patterns to reject:

- fake helpers where `print(...)` should be used
- wrong `print(...)` argument order in TIC-80 carts
- wrong primitive signatures such as `circ(...)` with extra arguments
- color ids outside the normal `0..15` range in ordinary cart drawing code

Playability blockers to fix before runtime:

- a player-controlled game with no real `btn(...)` / `btnp(...)` input path
- non-TIC screen assumptions like `320x180`
- menu or start screens with no actual input-driven transition into gameplay
- on-screen "press X to start/restart" text with no matching state mutation in code
- early `return` or silent auto-reset branches that skip the draw phase instead of giving a real game-over or restart loop
- pickup, delivery, combat, or score checks that are disconnected from the actual objects or coordinates drawn on screen
- palette-only or palette-first script carts with no real game code before the footer
- repeated invalid drawing signatures such as `circ(...)` with extra arguments or ordinary color ids outside `0..15`

## Workflow Guardrails

For bounded agent iteration, prefer this sequence:

1. read the one reference file that matches the next step
2. write or edit the cart
3. `tic80ctl start`
4. `tic80ctl load <cart>.lua`
5. `tic80ctl run`
6. use `eval`, `screenshot`, or `playtest` based on the next concrete question

Important:

- if the cart file changed, reload it before running it again
- if a write failed, was rejected, or never landed on disk, do not continue to `start`, `load`, or `run` as if the corrected cart exists yet
- prefer relative cart paths from the TIC filesystem root
- do not treat `run` alone as proof that the cart is playable
- if a cart is still structurally wrong, fix that before spending turns on environment or wrapper debugging
- if the cart is only "closer" but still fails the structural checks above, do not move to `tic80ctl start` yet
- if the task is already proven by a clean playtest, stop instead of restarting TIC-80 for extra archaeology
- if a playtest route completes without proving the in-game win state, do not narrate that as success; inspect whether the route or the game logic is wrong
- do not change the core goal or win condition just to make one playtest route pass unless the task explicitly allows that simplification

## Palette And Exactness

If a trailing TIC-80 palette block is required:

- keep it at the very end of the script cart
- use the exact wrapper lines and exactly 96 lowercase hex characters after `000:`
- do not start the file with the palette block and do not spend multiple turns hand-optimizing the footer before the cart itself works

But do not let palette counting dominate the session:

- structural cart correctness matters more than palette polish
- do not get stuck in repeated `wc`, `grep`, `sed`, `rg`, or `cat` counting loops
- if exactness is uncertain, make one bounded correction and return to runtime verification
- if a correct literal palette string is already available, prefer using it over re-counting by hand

## Start Here

Good first-use sequence:

```sh
tic80ctl start
tic80ctl load game.lua
tic80ctl run
tic80ctl eval "trace(type(TIC))"
```

That gives you:

- one live TIC-80 session
- your cart loaded
- the cart runtime started
- a quick proof that the runtime is alive

After that, use:

- `eval` for short probes
- `screenshot` for one visual check
- `playtest` for a multi-frame route
- `sfx`, `music`, `sprite`, and `map` commands when you want to edit cartridge content

## References

This skill depends on local references under `reference/`.

Useful references:

- `reference/tic80_console_and_runtime.md`
  - TIC-80 console commands, `eval`, runtime probing, script-cart usage, and `resume reload`
- `reference/tic80_project_workflow.md`
  - repo-root setup, external-editor flow, `require`-based multi-file projects, and shipping caveats
- `reference/scripted_playtest_guide.md`
  - playtest episode usage and debugging
- `reference/tic80_api_reference.md`
  - single authoritative TIC-80 callback, input, drawing, map, audio, memory, and debugging reference

Read only the file that matches the current task. Do not load the whole `reference/` directory by default.

## Session Commands

Use one long-lived session while iterating.

Core commands:

- `tic80ctl start`
- `tic80ctl status`
- `tic80ctl stop`

### `start`

Start TIC-80 from the project root you want to use as the TIC filesystem root.

Example:

```sh
tic80ctl start
```

Practical rules:

- start from the repo or project root
- keep carts, screenshots, and any Lua modules under that root
- load carts after `start`

### `status`

Check whether the session is alive.

Example:

```sh
tic80ctl status
```

### `stop`

Stop the active session.

Example:

```sh
tic80ctl stop
```

## Runtime Commands

Use these while working on code and gameplay.

### `cmd`

Send a raw TIC-80 console command.

Examples:

```sh
tic80ctl cmd "help commands"
tic80ctl cmd "load game.lua"
tic80ctl cmd "run"
tic80ctl cmd "eval trace(type(TIC))"
```

Use `cmd` when:

- you want exact console behavior
- there is no dedicated shortcut for the command
- you are exploring or debugging interactively

### `load`

Load a cart into the active session.

Example:

```sh
tic80ctl load game.lua
```

### `run`

Start the currently loaded cart.

Example:

```sh
tic80ctl run
```

If this fails immediately, the cart likely hit a runtime error during its first frame.

### `eval`

Run a short Lua expression in the active cart.

Examples:

```sh
tic80ctl eval "trace(type(TIC))"
tic80ctl eval "trace(player_x)"
tic80ctl eval "trace(frame)"
tic80ctl eval "some_flag = true"
```

Use `eval` for short runtime probes and toggles.

Good habit:

- call `run` before relying on `eval`

### `screenshot`

Capture one frame.

Examples:

```sh
tic80ctl screenshot
tic80ctl screenshot shots/frame.png
```

Important path rule:

- screenshot paths are relative to the active TIC filesystem root
- do not use absolute host paths

If the target subdirectory does not exist, create it first.

Use screenshots for:

- one visual confirmation
- before/after checks
- quick inspection outside a full playtest

## Editing Cartridge Content

`tic80ctl` can also edit cartridge data directly from the shell.

Top-level content groups:

- `tic80ctl sfx ...`
- `tic80ctl music ...`
- `tic80ctl sprite ...`
- `tic80ctl map ...`

Simple rule:

- selector only reads current content
- selector plus payload writes new content

## SFX Commands

Use SFX commands when shaping sound effects.

Think about the SFX editor like this:

1. `wavetable` sets the base waveform
2. `arpeggio` and `pitch` change note movement over time
3. `volume` shapes loudness over time
4. `panning` places the sound left or right
5. `speed` and `loop` control playback timing behavior

Available commands:

- `tic80ctl sfx wavetable <sfx> [hex32]`
- `tic80ctl sfx volume <sfx> [tick:value,...]`
- `tic80ctl sfx wave <sfx> [tick:value,...]`
- `tic80ctl sfx arpeggio <sfx> [csv]`
- `tic80ctl sfx pitch <sfx> [tick:value,...]`
- `tic80ctl sfx panning <sfx> [left,right]`
- `tic80ctl sfx speed <sfx> [value]`
- `tic80ctl sfx loop <sfx> <target> [start:size]`

Examples:

```sh
tic80ctl sfx wavetable 0
tic80ctl sfx wavetable 0 0123456789abcdef0123456789abcdef

tic80ctl sfx volume 0 0:15,8:8,31:0
tic80ctl sfx pitch 0 0:12,31:-24
tic80ctl sfx arpeggio 0 0,4,7,12
tic80ctl sfx panning 0 true,false
tic80ctl sfx speed 0 2
tic80ctl sfx loop 0 pitch 3:5
```

Payload notes:

- wavetable uses `32` hex digits
- volume, wave, and pitch use `tick:value,...`
- arpeggio uses comma-separated semitone values
- panning uses `left,right` booleans
- loop uses `start:size`

## Music Commands

Use music commands when editing tracker data.

Available commands:

- `tic80ctl music track <track> [tempo,speed,rows]`
- `tic80ctl music frame <track> <frame> [p0,p1,p2,p3]`
- `tic80ctl music row <pattern> <row> [note:sfx:cmd]`
- `tic80ctl music rows <pattern> <rows>`

Examples:

```sh
tic80ctl music track 0
tic80ctl music track 0 140,5,64

tic80ctl music frame 0 0
tic80ctl music frame 0 0 1,2,3,4

tic80ctl music row 1 5
tic80ctl music row 1 5 C-4:2:F1a

tic80ctl music rows 1 1,7,12
tic80ctl music rows 1 1:C-4:2:F1a,7:OFF:-:-
```

Practical split:

- use `track` and `frame` for song structure
- use `row` and `rows` for note data inside patterns

## Sprite And Palette Commands

Use sprite commands for tile, region, and palette editing.

Available commands:

- `tic80ctl sprite tile <id> [row0,...,row7]`
- `tic80ctl sprite region [--bank N] <x> <y> <width> <height> [tile;tile;...]`
- `tic80ctl sprite palette [--bank N] [--vbank N] [RRGGBB,...]`

Examples:

```sh
tic80ctl sprite tile 3
tic80ctl sprite tile 3 01234567,89abcdef,01234567,89abcdef,01234567,89abcdef,01234567,89abcdef

tic80ctl sprite region --bank 1 2 4 2 1
tic80ctl sprite region --bank 1 2 4 2 1 01234567,89abcdef,01234567,89abcdef,01234567,89abcdef,01234567,89abcdef;fedcba98,76543210,fedcba98,76543210,fedcba98,76543210,fedcba98,76543210

tic80ctl sprite palette
tic80ctl sprite palette --bank 1 --vbank 1 000000,111111,222222,333333,444444,555555,666666,777777,888888,999999,aaaaaa,bbbbbb,cccccc,dddddd,eeeeee,ffffff
```

Payload notes:

- sprite tiles use `8` rows of `8` hex digits
- sprite regions use semicolon-separated tiles in row-major order
- palette writes use `16` comma-separated `RRGGBB` colors

## Map Commands

Use map commands for level layout.

Available commands:

- `tic80ctl map rect [--bank N] <x> <y> <width> <height> [tile]`
- `tic80ctl map chunk [--bank N] <x> <y> <width> <height> [csv]`

Examples:

```sh
tic80ctl map rect 5 7 3 2
tic80ctl map rect 5 7 3 2 9

tic80ctl map chunk 10 12 3 2
tic80ctl map chunk 10 12 3 2 1,2,3,4,5,6
```

Use:

- `map rect` for broad fills
- `map chunk` for local detail

For `map chunk`, the payload must contain exactly `width*height` tile ids in row-major order.

## Text Cart Workflow

For larger projects, use a text-cart workflow.

Good default:

1. `tic80ctl start`
2. `tic80ctl load game.lua`
3. edit code in an external editor
4. use `run`, `eval`, `screenshot`, and `playtest`
5. return to asset commands only when you need to edit SFX, music, sprites, or the map

Helpful habits:

- keep one session alive while iterating
- start from the project root
- avoid editing the same asset in multiple places at once
- use `reference/tic80_practical_workflow.md` if the project starts to grow

## Playtest

Use `playtest` when you want to drive the game over many frames and inspect the result.

Syntax:

- `tic80ctl playtest --script-file <file>`
- `tic80ctl playtest --script-file <file> --timeout <seconds>`
- `tic80ctl playtest --script-file <file> --no-input-overlay`

Examples:

```sh
tic80ctl playtest --script-file episode.lua
tic80ctl playtest --script-file episode.lua --timeout 5 --no-input-overlay
```

Use playtest for:

- traversal routes
- combat checks
- progression checks
- before/after proof runs

Prefer playtest over repeated one-frame shell pokes when the question spans multiple frames.

## Playtest Script API

Use only these functions in episode scripts:

- `frameadvance()`
- `set_input(input_table)`
- `set_input(player_num, input_table)`
- `log(text)`
- `end_episode(status, message)`

### `frameadvance()`

Advance exactly one gameplay frame.

### `set_input(input_table)`

Set player 1 input for the next frame.

Example:

```lua
set_input({right=true, a=true})
frameadvance()
```

### `set_input(player_num, input_table)`

Set another player explicitly.

Example:

```lua
set_input(2, {left=true, b=true})
frameadvance()
```

### Valid Buttons

- `up`
- `down`
- `left`
- `right`
- `a`
- `b`
- `x`
- `y`

### Input Rules

- unspecified buttons default to `false`
- input lasts for one frame
- `set_input(...)` prepares the next frame
- `frameadvance()` consumes that input

### `log(text)`

Write a short label into the playtest log.

Use it for:

- route segment names
- checkpoints
- experiment labels

### `end_episode(status, message)`

End the episode deliberately.

Examples:

```lua
end_episode("done", "baseline")
end_episode("success", "reached exit")
end_episode("failure", "player died")
```

## Writing Good Playtests

Write playtests as short named routes.

Good pattern:

- start game
- move through a known segment
- press actions at deliberate moments
- log each segment
- end with a success or failure message

Example helper:

```lua
local function hold(input, frames, label)
  if label then log(label) end
  for i=1,frames do
    set_input(input)
    frameadvance()
  end
  set_input({})
end
```

Example route labels:

- `start game`
- `cross first lane`
- `collect key`
- `climb to upper route`
- `reach exit`

## `DEBUG_MODE` During Playtest

For Lua carts, `playtest` enables `DEBUG_MODE=true` during the episode and clears it afterward.

That is useful for debug-only rendering and tracing, for example:

```lua
if DEBUG_MODE then
  rectb(player.x-2, player.y-2, 20, 20, 2)
  trace("debug player_x="..player.x)
end
```

Good uses:

- hitboxes
- room ids
- patrol paths
- collision probes
- camera zones
- debug-only `trace(...)`

## Playtest Artifacts

Expect output under:

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

Meaning:

- `script.lua` is the script that ran
- `log.txt` contains `log(...)` output
- `console.txt` contains cart-side `trace(...)` output
- `screenshots/` contains one image per advanced frame

## Troubleshooting

Useful interpretations:

- `tic80ctl: no active session`
  - run `tic80ctl start`
- `unknown command: ...`
  - the console command itself is invalid
- immediate error from `tic80ctl run`
  - the cart failed during its first run-mode frame
- `path must be relative to the TIC filesystem root`
  - the screenshot path is invalid
- `relative screenshot directory does not exist: <dir>`
  - create the target subdirectory first
- `function` from `tic80ctl eval "trace(type(TIC))"`
  - the runtime exists
- empty `console.txt`
  - the cart did not call `trace(...)` during the episode

## Good Habits

Use these consistently:

- keep one session alive while iterating
- use `load`, `run`, and `eval` for setup and quick probes
- use `playtest` for multi-frame questions
- use `screenshot` for one frame
- inspect artifact paths instead of guessing
- check shell exit codes in automation

## Quick Decision Rule

Use this split:

- one TIC-80 console command: `cmd` or an alias
- one current frame: `screenshot`
- one multi-frame route or experiment: `playtest`
- edit cartridge content: `sfx`, `music`, `sprite`, or `map`
