# Scripted Playtest Episodes

Use `tic80ctl playtest` when you need to run one bounded gameplay experiment and inspect the resulting evidence.

Treat one episode as:

- one planned route or scenario
- one deterministic input timeline
- one artifact directory you can inspect afterward

This is the right abstraction for agent-driven TIC-80 iteration. Prefer it over many tiny manual control calls when you want to validate gameplay, progression, or visual state across multiple frames.

## Mental Model

Think in this loop:

1. load the cart
2. run the cart if needed
3. write a short episode script
4. run one `playtest`
5. inspect the returned artifacts
6. revise the cart or the script
7. rerun the same episode

The goal is not to simulate live controller play. The goal is to author a compact, reproducible test route and collect evidence from the result.
Each `playtest` starts by re-running the loaded cart, so every episode begins from a fresh cart boot instead of inheriting prior live-session state.
That means any state staged with `tic80ctl eval` before `playtest` is discarded when the episode starts.

## `tic80ctl playtest`

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

Do not expect state to carry from one `playtest` invocation to the next.
If you need a longer route, keep it inside one episode script.

## Targeted Playtests Need Cart-Side Boot Logic

Fresh-boot playtests are good for reproducibility, but they are not always the cheapest question to ask.
If the agent wants to validate "just level 2" or "just the escape sequence", the cart should own that section-loading logic itself.

Do not stage the state with `tic80ctl eval` and then call `playtest`.
`playtest` restarts the cart, so that live runtime state will not carry into the episode.

This avoids adding one-off flags to `tic80ctl`.
The tool stays generic, while the game owns the meaning of "level 2", "checkpoint core_entry", or "boss intro".

Preferred pattern:

1. add a cart-side reset/boot helper that can enter the target section
2. gate that path behind explicit debug-only logic such as `DEBUG_MODE`
3. keep the normal player-facing startup path separate
4. run a short `playtest` for the target segment

### Why This Pattern Exists

Use this when:

- the cart has many levels
- the interesting bug is deep into the game
- replaying from boot would waste time
- the game already has level/checkpoint loading logic you can reuse

This gives you a deterministic setup without expanding the `tic80ctl` command surface.
It also keeps the test setup honest: the episode still starts from a restart, but the cart deliberately chooses a debug-only entry section during that restart.

### Example: Start At Level 2

Cart-side boot/reset helper:

```lua
PLAYTEST_SECTION = nil

local function load_level(id)
  current_level = id
  player.x = level_spawns[id].x
  player.y = level_spawns[id].y
  enemies = spawn_level_enemies(id)
  camera_x = level_camera_x(id)
end

function reset_game()
  show_title_screen()

  if DEBUG_MODE and PLAYTEST_SECTION == "level2" then
    load_level(2)
    game_state = "play"
  end
end
```

That answers "does level 2 behave correctly from its intended entry state?" without replaying level 1 every time, while still letting the episode begin from a true restart.

Shell flow:

```sh
tic80ctl load game.lua
tic80ctl playtest --script-file level2_route.lua
```

### Example: Start From A Named Checkpoint

Cart-side helper:

```lua
PLAYTEST_SECTION = nil

function restore_checkpoint(name)
  local cp = checkpoints[name]
  current_level = cp.level
  player.x = cp.x
  player.y = cp.y
  player.hp = cp.hp
  inventory.key = cp.key
  reactor_armed = cp.reactor_armed
end

function reset_game()
  start_new_game()

  if DEBUG_MODE and PLAYTEST_SECTION == "core_entry" then
    restore_checkpoint("core_entry")
  end
end
```

Shell flow:

```sh
tic80ctl load danger_zone.lua
tic80ctl playtest --script-file core_escape.lua
```

That is the right fix when the bug only matters near the core area and the agent should not have to manually replay the whole route first.

### Practical Rules

Prefer a named helper when:

- multiple tests need the same setup
- the setup touches several globals
- the state has game-specific meaning
- you want the repo to explain what "core_entry" or "level 2 start" means

Prefer plain `run` + `eval` + `screenshot` instead of `playtest` when:

- you are probing a one-off live-runtime hypothesis
- the setup is only one or two assignments
- you explicitly do not want a restart between setup and observation

Good habit:

- if a targeted playtest needs a special entry point, encode it in the cart's debug-only reset path instead of relying on carried state

Typical output includes:

- `status=...`
- `message=...`
- `artifact_path=./playtest/episode_n`
- `frames=...`

If the episode errors or times out, keep the artifact and inspect what was produced before the failure.

## Episode Script API

The script surface is intentionally small.

Use only:

- `frameadvance()`
- `set_input(input_table)`
- `set_input(player_num, input_table)`
- `log(text)`
- `end_episode(status, message)`

### `frameadvance()`

Advance the game by exactly one frame.

This:

- consumes the currently prepared input
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
- notes about expected behavior

### `end_episode(status, message)`

Terminate the episode deliberately.

Examples:

```lua
end_episode("done", "baseline")
end_episode("success", "reached exit")
end_episode("failure", "player died")
```

Use short, explicit status strings. The message should tell you what happened in plain language.

## Good Episode Style

Write scripts that model a real route:

- start the test condition
- hold deliberate inputs for known spans
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

Good log labels:

- `start game`
- `cross first lane`
- `collect key`
- `reach door`
- `jump final gap`
- `reach exit`

Use labels that make failures easy to localize when you inspect `log.txt` and the screenshots.

## Artifact Layout

Expect a directory like:

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

## What To Inspect

After a run, inspect:

- `status` and `message`
- `log.txt`
- `console.txt`
- a few key screenshots

Use this order:

1. confirm the route reached the expected segment in `log.txt`
2. check `console.txt` for cart-side facts from `trace(...)`
3. inspect the frames around the failure or success point

Do not inspect every screenshot if a few labeled segments can answer the question.

## Debugging Pattern

Use cart-side debug instrumentation during episodes.

For Lua carts, gate debug visuals behind `DEBUG_MODE` when practical:

```lua
if DEBUG_MODE then
  rectb(player.x-2, player.y-2, player.w+4, player.h+4, 2)
  trace("PX "..player.x.." PY "..player.y)
end
```

Useful episode-time debug output:

- hitboxes
- room ids
- patrol paths
- target positions
- state labels
- collision probes

Use `trace(...)` for facts you want in `console.txt`. Use `log(...)` in the episode script for route annotations.

## Determinism

Prefer episodes you can rerun exactly:

- same cart
- same script
- same expected route

If the cart uses randomness, seed or constrain it when possible so repeated runs stay comparable.

## Practical Rules

- Prefer one short, focused episode over one giant script.
- Use `playtest` for gameplay/progression validation.
- Use `screenshot` for one-off visual checks.
- Use `eval` for short runtime probes or toggles.
- Reuse a passing episode as a regression check after code changes.

The most useful outcome is not “the agent pressed buttons.” The most useful outcome is “the agent can rerun the same route and compare stable evidence between revisions.”
