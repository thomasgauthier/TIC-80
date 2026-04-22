# TIC-80 Project Workflow

Use this file for repo-level workflow: filesystem root choices, multi-file organization, external modules, and shipping caveats. It is intentionally focused on what an agent needs while editing and testing carts.

## Recommended Working Model

For agent-driven iteration, prefer this loop:

1. edit the cart or module files
2. run one bounded check with `eval`, `screenshot`, or `playtest`
3. inspect the evidence
4. revise
5. repeat

This keeps debugging reproducible and avoids control churn.

For bounded babysat tasks, also prefer:

- one small script cart before any multi-file expansion
- one playable core loop before extra systems, decorative scaffolding, or large asset work
- structural correctness before palette polish or footer perfection
- one exact structural correction followed by a materially different rewrite, not a cosmetic retry inside the same broken family

Do not treat these as meaningful progress:

- adding `function TIC()` while keeping old `load()` / `draw()` / `main()` wrappers
- producing a large cart that still uses the wrong API family
- repeated palette rewrites while the gameplay loop is still structurally wrong
- changing on-screen control labels without adding the actual start/restart/game-state transitions they promise
- changing helper names while keeping the same invalid drawing signatures or disconnected gameplay state underneath

## Pre-Runtime Self-Check

Before `tic80ctl start`, make sure the cart is already worth running:

- one global `function TIC()` exists
- persistent game state is initialized outside `TIC()`
- the player-controlled loop has a real `btn(...)` or `btnp(...)` input path
- any start, restart, or menu text has matching state-transition code
- pickup, delivery, combat, or win checks are tied to the actual coordinates or objects you draw
- screen math stays inside normal `240x136` assumptions unless a local source says otherwise
- required palette block is the literal final lines of the file, not the first lines

If one of these is still false:

- edit the cart again before spending a step on `tic80ctl`
- do not act as if a rejected or failed rewrite already exists on disk

## Start TIC-80 From The Project Root

Treat the repo or game directory as the TIC filesystem root so carts, modules, screenshots, and playtest artifacts all live under the project.

Canonical launch shape:

```sh
tic80 --fs . --cmd="load main.lua"
```

In practice, `tic80ctl start` should be run from the project root you want TIC-80 to see.

## External Editor Workflow

Preferred Lua workflow:

1. create or edit art/audio/map resources in TIC-80
2. `save mygame.lua`
3. edit Lua code in the saved file with a normal editor
4. switch back to TIC-80 to run or adjust resources

Important constraints:

- script carts contain code at the top and tagged resource data at the bottom
- edit the code section, not the serialized resource blocks
- TIC-80 can notice file changes and reload them
- do not keep conflicting unsaved edits in both places

## Multi-File Lua Projects

TIC-80 Lua can load external modules with `require`.

Basic shape:

```lua
require "libraries/math"
require "libraries/table"
```

Recommended habits:

- keep `require` calls near the top of the main cart
- keep most coordination in the main cart and push helpers into modules
- keep paths relative and simple

If TIC-80 cannot find the modules automatically, extend `package.path`:

```lua
package.path = package.path .. ";/path/to/project/?.lua"
```

Practical module notes:

- external files must remain available at runtime
- requiring many small modules is fine during development
- final distribution may need a single-file cart instead

For small agent tasks, do not jump to multi-file structure by default.

Use modules only when:

- the single-file cart is already structurally correct
- the added file split clearly reduces complexity
- the path behavior is already understood

## Shipping Caveat

If the game depends on external Lua files, exported players or packaged builds also need those files unless you bundle them back into one cart. Multi-file structure is a development convenience, not automatically a portable release format.

## Reload And State Design

If you plan to use `resume reload`, design state so it survives code reloads:

- keep game state in one root table when possible
- store that table in `_G`
- avoid scattering critical state across many unrelated globals

Example:

```lua
local state

if _G.state then
  state = _G.state
else
  state = {
    room = 1,
    player = {x = 32, y = 64},
    enemies = {},
  }
  _G.state = state
end
```

This also makes runtime inspection easier through `eval`.

## When To Use Which Validation Tool

- use `tic80ctl eval` for one fact or one quick toggle
- use `tic80ctl screenshot` for a single visual confirmation
- use `tic80ctl playtest` for anything that depends on multiple frames, route flow, or an artifact trail

Do not substitute a long chain of manual probes for a short deterministic playtest when the behavior spans time.

Do not substitute:

- repeated character counting for one bounded palette correction
- repeated screenshots for a scripted progression check
- environment archaeology for the next direct `load`, `run`, or `playtest` step

## Useful Editing Habit

Inside TIC-80, `CTRL+O` opens the code outline. It is still useful even when most editing happens outside TIC-80, especially for inspecting large carts quickly.
