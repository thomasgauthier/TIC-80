# TIC-80 Lua API Quick Reference

Use this reference when you need a fast reminder of the most common TIC-80 Lua APIs while working through `tic80ctl`.

For broad coverage, also consult:

- `reference/source_api.md`
- `reference/source_api_cheatsheet.md`

## Core Callback

Every cart needs `TIC()`:

```lua
function TIC()
  cls()
  update_game()
  draw_game()
end
```

Useful mental model:

- code outside `TIC()` runs at startup
- `TIC()` runs every frame

## Input

### `btn(id) -> pressed`

Read gamepad button state for the current frame.

Common player-1 button ids:

- `0` up
- `1` down
- `2` left
- `3` right
- `4` A
- `5` B
- `6` X
- `7` Y

Example:

```lua
if btn(2) then player.x = player.x - 1 end
if btn(3) then player.x = player.x + 1 end
```

### `btnp(id, hold=-1, period=-1) -> pressed`

Read button press transitions rather than held state.

Use `btnp` for:

- menu confirms
- spawning on a single press
- toggles
- one-shot actions

Example:

```lua
if btnp(4) then spawn_enemy() end
```

### `key(code)`, `keyp(code)`

Keyboard equivalents for current-state and pressed-this-frame checks.

Use them when the cart relies on keyboard debugging or editor-style controls.

## Drawing

### `cls([color=0])`

Clear the screen.

Default safe pattern:

```lua
function TIC()
  cls()
  ...
end
```

### `print(text, x=0, y=0, color=12, fixed=false, scale=1, smallfont=false)`

Draw text to the screen.

Example:

```lua
print("score: "..score, 4, 4, 12)
```

### `spr(id, x, y, transparent=-1, scale=1, flip=0, rotate=0, w=1, h=1)`

Draw one sprite or a composite sprite.

Example:

```lua
spr(1, player.x, player.y, 0, 1)
```

### `map(x=0, y=0, w=30, h=17, sx=0, sy=0, colorkey=-1, scale=1, remap=nil)`

Draw a region of the map.

Use it for tile-based rooms, world layers, or background rendering.

### Basic shapes

Useful for debug overlays and quick effects:

- `rect(x, y, w, h, color)`
- `rectb(x, y, w, h, color)`
- `line(x0, y0, x1, y1, color)`
- `circ(x, y, radius, color)`
- `circb(x, y, radius, color)`
- `pix(x, y [,color])`

## Map And Flags

### `mget(x, y) -> id`

Read a tile from the map.

### `mset(x, y, id)`

Write a tile to the map.

### `fget(sprite_id, flag) -> bool`

Read sprite flags.

### `fset(sprite_id, flag, bool)`

Write sprite flags.

These are high-value for:

- collision tagging
- hazard tagging
- navigation hints
- data-driven map logic

## Debugging And Runtime Inspection

### `trace(msg [,color])`

Print to the TIC console.

Use this constantly during agent iteration.

Examples:

```lua
trace("player_x="..player.x)
trace("room="..current_room, 8)
```

### `time() -> milliseconds`

Milliseconds since the game started.

Useful for:

- simple timers
- profiling rough behavior
- cooldown logic

## Sound

### `sfx(id, note, duration=-1, channel=0, volume=15, speed=0)`

Play or stop a sound effect.

### `music(track=-1, frame=-1, row=-1, loop=true)`

Play or stop music.

Use these sparingly during debugging unless audio behavior is part of the test.

## Persistent And Low-Level State

### `pmem(index [,val]) -> val`

Read or write persistent memory.

Use for:

- save flags
- best score
- unlocks

### Memory APIs

Available for advanced work:

- `peek`, `peek1`, `peek2`, `peek4`
- `poke`, `poke1`, `poke2`, `poke4`
- `memcpy`
- `memset`
- `sync`
- `vbank`

These are powerful but lower-frequency for normal gameplay iteration. Reach for them only when the cart genuinely needs memory- or bank-level tricks.

## High-Frequency Agent Loop

The most common agent-facing pattern is:

1. `tic80ctl load <cart>`
2. `tic80ctl run`
3. inspect with `tic80ctl eval "trace(...)"` or a scripted `playtest`
4. rely on these APIs inside the cart:
   - `TIC`
   - `btn` / `btnp`
   - `cls`
   - `spr`
   - `map`
   - `print`
   - `mget` / `mset`
   - `fget` / `fset`
   - `trace`

If you need the full surface, use the copied source references rather than trying to memorize everything from this quick reference.
