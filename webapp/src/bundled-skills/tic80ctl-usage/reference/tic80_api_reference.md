# TIC-80 API Reference

Use this file as the single TIC-80 runtime/API reference for agent work. It keeps the common Lua surface, callback order, button ids, and the few low-level details that matter during debugging. It omits deprecated and duplicate explanations.

## Core Model

- Display: `240x136`, 16 colors.
- Main loop: `TIC()` runs at 60 fps.
- Code outside callbacks runs once at startup or reload.
- Use `trace(...)` for runtime facts and `cls()` at the start of `TIC()` unless persistent drawing is intentional.

## Callback Order

Define only the callbacks you need.

- `BOOT()`:
  runs once when the cart boots.
- `TIC()`:
  main update/draw callback. Required for normal carts.
- `OVR()`:
  optional overlay pass after `TIC()`.
- `BDR(row)`:
  optional per-scanline hook for palette or raster effects.
- `MENU(index)`:
  optional custom game-menu handler.

Practical frame order:

```lua
BOOT()      -- once at startup
TIC()       -- once per frame
OVR()       -- optional overlay
BDR(row)    -- optional per scanline
```

Do not build new code around `SCN`; use `BDR` instead.

## Input

### Gamepad

`btn(id) -> bool`

- true while held in the current frame.

`btnp(id, hold=-1, period=-1) -> bool`

- true on the press edge.
- with `hold` and `period`, repeats while held.

Player button ids:

| Action | P1 | P2 | P3 | P4 |
| --- | ---: | ---: | ---: | ---: |
| Up | 0 | 8 | 16 | 24 |
| Down | 1 | 9 | 17 | 25 |
| Left | 2 | 10 | 18 | 26 |
| Right | 3 | 11 | 19 | 27 |
| A | 4 | 12 | 20 | 28 |
| B | 5 | 13 | 21 | 29 |
| X | 6 | 14 | 22 | 30 |
| Y | 7 | 15 | 23 | 31 |

Typical use:

```lua
if btn(2) then player.x = player.x - 1 end
if btnp(4) then jump() end
```

### Keyboard And Mouse

- `key(code) -> bool`
- `keyp(code, hold=-1, period=-1) -> bool`
- `mouse() -> x, y, left, middle, right, scrollx, scrolly`

Useful keyboard codes:

- letters `A-Z`: `1-26`
- digits `0-9`: `27-36`
- arrows: `58-61`
- `space=48`, `tab=49`, `return=50`, `backspace=51`
- modifiers: `ctrl=63`, `shift=64`, `alt=65`

## Drawing

### Frame Control

- `cls([color=0])`
- `clip(x, y, w, h)` or `clip()`

### Text And Sprites

- `print(text, x=0, y=0, color=12, fixed=false, scale=1, smallfont=false) -> width`
- `font(text, x, y, transparent=-1, charw=8, charh=8, fixed=false, scale=1) -> width`
- `spr(id, x, y, transparent=-1, scale=1, flip=0, rotate=0, w=1, h=1)`
- `map(x=0, y=0, w=30, h=17, sx=0, sy=0, colorkey=-1, scale=1, remap=nil)`

`spr` notes:

- `flip`: `1` horizontal, `2` vertical, `3` both.
- `rotate`: `1` 90, `2` 180, `3` 270 degrees clockwise.

`map` notes:

- `x, y, w, h` address map cells.
- `sx, sy` place the drawn output on screen.
- `remap(tile, x, y) -> tile, flip, rotate` can override per-tile rendering.

### Primitives

- `pix(x, y [,color]) -> color`
- `line(x0, y0, x1, y1, color)`
- `rect(x, y, w, h, color)`
- `rectb(x, y, w, h, color)`
- `circ(x, y, radius, color)`
- `circb(x, y, radius, color)`
- `elli(x, y, rx, ry, color)`
- `ellib(x, y, rx, ry, color)`
- `tri(x1, y1, x2, y2, x3, y3, color)`
- `trib(x1, y1, x2, y2, x3, y3, color)`
- `ttri(x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3, texsrc=0, chromakey=-1, z1=0, z2=0, z3=0)`

Common agent mistakes to reject quickly:

- `circ(...)` with more than four arguments
- `rect(...)` without the final color argument
- invented wrappers such as `fill_rect(...)`, `set_color(...)`, or drawing-state `set(...)`
- ordinary cart color ids outside `0..15`

## Map, Flags, And Cart Data

- `mget(x, y) -> tile_id`
- `mset(x, y, tile_id)`
- `fget(sprite_id, flag) -> bool`
- `fset(sprite_id, flag, bool)`

Use sprite flags for collision tags, hazards, or simple tile metadata instead of hard-coding tile ids everywhere.

## Audio

- `sfx(id, note=nil, duration=-1, channel=0, volume=15, speed=0)`
- `music(track=-1, frame=-1, row=-1, loop=true)`

Agent workflow note:

- audio APIs matter when verifying gameplay feedback, but most code/debug loops should focus on visible state plus `trace(...)`.

## Memory And Persistence

- `pmem(index [,value]) -> value`
- `peek(addr [,bits=8]) -> value`
- `peek1(bitaddr) -> bit`
- `peek2(addr2) -> value`
- `peek4(addr4) -> value`
- `poke(addr, value)`
- `poke1(bitaddr, bit)`
- `poke2(addr2, value)`
- `poke4(addr4, value)`
- `memcpy(toaddr, fromaddr, len)`
- `memset(addr, value, len)`
- `sync(mask=0, bank=0, tocart=false)`
- `vbank(bank)`

`sync` mask bits:

```lua
tiles   = 1 << 0
sprites = 1 << 1
map     = 1 << 2
sfx     = 1 << 3
music   = 1 << 4
palette = 1 << 5
flags   = 1 << 6
screen  = 1 << 7
```

Use these low-level APIs only when the cart truly needs memory tricks, banked video state, or persistence. For ordinary gameplay work, `mget`, `mset`, `fget`, `fset`, and regular Lua tables are usually enough.

## System Utilities

- `trace(msg [,color])`
- `time() -> milliseconds_since_boot`
- `tstamp() -> unix_seconds`
- `reset()`
- `exit()`

High-value debugging pattern:

```lua
trace("player=" .. player.x .. "," .. player.y)
trace("room=" .. state.room, 8)
```

## RAM Facts Worth Keeping

Only keep these in working memory when debugging low-level issues:

- screen VRAM starts at `0x00000`
- tiles start at `0x04000`
- sprites start at `0x06000`
- map starts at `0x08000`
- persistent memory starts at `0x14004`
- sprite flags start at `0x14404`

If you are not using `peek*` or `poke*`, you do not need the full RAM layout.

## Minimal Cart Pattern

```lua
player = player or {x = 96, y = 24}

function TIC()
  cls()
  if btn(0) then player.y = player.y - 1 end
  if btn(1) then player.y = player.y + 1 end
  if btn(2) then player.x = player.x - 1 end
  if btn(3) then player.x = player.x + 1 end
  spr(1, player.x, player.y, 0)
  print("HELLO", 84, 84, 12)
end
```

## Agent Priorities

When writing or debugging TIC-80 gameplay code, the highest-value API surface is usually:

1. `TIC`, `BOOT`, `BDR`
2. `btn`, `btnp`, `key`, `keyp`
3. `cls`, `print`, `spr`, `map`
4. `mget`, `mset`, `fget`, `fset`
5. `trace`, `time`, `pmem`

Reach for the lower-level memory APIs only when the problem specifically requires them.
