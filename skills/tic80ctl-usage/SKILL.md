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

- `reference/tic80_practical_workflow.md`
  - console, reload, external-editor, and multi-file workflow notes
- `reference/scripted_playtest_guide.md`
  - playtest episode usage and debugging
- `reference/tic80_lua_api_quickref.md`
  - quick reminders for common TIC-80 Lua APIs
- `reference/official_tic80_learn_reference.md`
  - broad TIC-80 reference, console commands, RAM layout, and API catalog
- `reference/source_console.md`
- `reference/source_reload.md`
- `reference/source_external_editor.md`
- `reference/source_require_workflow.md`
- `reference/source_api.md`
- `reference/source_api_cheatsheet.md`

Use those when you need deeper TIC-80 behavior beyond the `tic80ctl` command surface itself.

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
