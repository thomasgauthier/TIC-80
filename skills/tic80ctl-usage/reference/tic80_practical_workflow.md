# Practical TIC-80 Workflow Notes

This reference captures high-signal workflow details that are useful when operating TIC-80 through `tic80ctl`.

It is not a full manual. It focuses on console/runtime habits, debugging, external-editor flow, multi-file organization, and reload-friendly development.

## Console Commands Worth Remembering

These are the commands most worth keeping in your active working set:

- `new lua`
  - create a new Lua cart
- `load <cart>`
  - load a cart or script file
- `save <cart>`
  - save the current cart
- `run`
  - start the loaded cart
- `resume`
  - resume the last run
- `resume reload`
  - reload code before resuming the current run
- `folder`
  - open or reveal the active TIC filesystem root
- `dir` / `ls`
  - inspect the TIC-visible directory
- `cd <path>`
  - move around the TIC-visible directory
- `mkdir <name>`
  - create a directory
- `eval ...`
  - run a short code snippet in the current runtime
- `help`
  - inspect commands, keys, API, and startup info

Useful habits:

- command history and tab completion are available in the console
- `load` does not require the `.tic` suffix
- `folder` is the fastest way to confirm where TIC-80 thinks the working directory is

## Runtime And Debugging Gotchas

### `eval` needs a live runtime

Run the cart first. If the game VM is not running, `eval` can produce an empty string and mislead you.

Reliable pattern:

1. `load <cart>`
2. `run`
3. `eval trace(...)`

### Use `trace(...)` for debugging

Use `trace(...)` to emit facts to the console.

Examples:

```lua
trace("x="..x)
trace("state="..state.mode, 8)
```

Useful notes:

- `trace` is the right tool for runtime facts you want to inspect from the console
- `cls` clears console output if the trace stream gets noisy
- concatenation in Lua uses `..`

### Clear the screen unless you deliberately want persistence

If you do not clear the screen each frame, you can get visual artifacts.

Default safe pattern:

```lua
function TIC()
  cls()
  update_game()
  draw_game()
end
```

### `TIC()` is the frame loop

Treat `TIC()` as the main update/draw loop at roughly 60fps.

Useful mental model:

- code outside `TIC()` runs at startup
- code inside `TIC()` runs every frame

## Reload-Friendly Development

`resume reload` is valuable during debugging because it lets you patch code and continue from the current situation instead of replaying the whole setup from the start.

This works best if your game state is stored in one table and initialization is guarded.

Reload-friendly pattern:

```lua
local state

if _G.state then
  state = _G.state
else
  state = {
    x = 64,
    y = 128,
    shots = {},
    enemies = {},
  }
  _G.state = state
end
```

Why this matters:

- code can be reloaded without wiping the current test situation
- `eval` can inspect or mutate `_G.state`
- one central state table is easier to preserve than scattered globals

Be careful with tables that carry methods or closures. Reloading code does not automatically update every existing object in place.

## External Editor Workflow

For text-based development, TIC-80 can work well with an external editor.

High-signal flow:

1. create or edit resources in TIC-80
2. save as a script-format cart such as `save mygame.lua`
3. edit the code at the top of the file in your editor
4. switch back to TIC-80 to run or edit resources

Important notes:

- in script-format carts, resource data lives at the bottom in tagged blocks
- add or change code at the top of the file
- avoid manually editing resource blocks unless you mean to
- avoid having unsaved changes in TIC resource editors and your external editor at the same time

TIC-80 can automatically notice file changes and reload them while it is open, which makes this flow efficient.

## Working From A Repo Directory

For bigger projects, use the project directory as the TIC filesystem root instead of relying on the default data folder.

The practical launch shape is:

```sh
tic80 --fs . --cmd="load main.lua"
```

The key idea is:

- `--fs .` makes the current project directory the TIC-visible filesystem root
- `load main.lua` then works relative to the repo directory

This is especially useful for git-backed projects and for `tic80ctl` sessions started from a repo root.

## Multi-File Organization

TIC-80 Lua projects can be split into multiple files with `require`.

Simple pattern:

```lua
require "libraries/math"
require "libraries/table"
```

Recommended habit:

- keep `require` calls near the top of the main cart
- treat the main cart as the coordination layer
- keep helpers and subsystems in separate files when the project grows

Why this helps:

- code gets easier to navigate
- systems become easier to test and rewrite
- large carts stay manageable

If TIC-80 cannot find required files automatically, `package.path` can be extended explicitly.

Example:

```lua
package.path = package.path..";/path/to/project/?.lua"
```

## Shipping Caveat For External Files

If the cart depends on external Lua files, those files must be available at runtime.

That means:

- exported players or distributed builds may also need the module files
- if you want a single-file deliverable, combine or inline the external files before export

During development, multi-file structure is excellent.
For shipping, reduce that structure to whatever the final distribution format requires.

## Useful Editor Habit

`CTRL+O` in the code editor shows an outline and is one of the fastest ways to navigate a large TIC-80 cart.

Even if most editing happens externally, it is still useful when inspecting or patching code inside TIC itself.
