---
name: webtui-pi-extension-port
description: Port a Pi coding-agent extension into the TIC-80 webapp browser TUI, bundle it as a browser-safe resource, load it through the browser resource loader, and wire it into the active MCP-backed workspace/runtime. Use when adding, debugging, or handing off webapp Pi extension support.
---

# Webapp Pi Extension Port Handoff

Use this skill when you need to add or maintain a Pi extension inside:

- `./webapp/`

for the browser-hosted Pi TUI integrated with TIC-80.

This is **not** the same as adding a normal Node/CLI Pi extension.
The webapp runs Pi inside a browser bundle with shims and polyfills, so extensions must be ported and loaded in a browser-specific way.

This file is an exhaustive handoff for the current approach.

---

# Scope

This skill explains how to:

1. decide whether a Pi extension is portable to the webapp
2. port a Node-style extension to a browser-safe version
3. bundle its source into `webapp/`
4. expose it as a loaded Pi resource in the browser resource loader
5. ensure Pi's extension runtime actually executes it
6. make it visible at a project-style path like:
   - `/workspace/.pi/extensions/...`
7. keep it working against the active filesystem:
   - in-memory before TIC-80 takeover
   - MCP-backed after TIC-80 session startup

---

# Current Architecture Summary

The webapp Pi TUI is not a full generic Pi runtime. It is a browser-adapted Pi environment with:

- real `AgentSession`
- real `InteractiveMode`
- a browser `WebTerminal`
- browser shims/polyfills for many Node-dependent Pi subsystems
- browser-specific resource loading
- runtime filesystem switching onto TIC-80 MCP FS

The extension system in full Pi is Node-oriented, but in the webapp we currently use a browser-specific port path.

## Current extension integration files

Primary webapp files involved in extension support:

- `webapp/src/browser-resource-loader.ts`
- `webapp/src/shims/browser-extensions.ts`
- `webapp/src/bundled-extension.ts`
- `webapp/src/bundled-extension-runtime.ts`
- `webapp/src/bundled-extensions/`
- `webapp/src/main.ts`

Related runtime/filesystem pieces:

- `webapp/src/browser-workspace.ts`
- `webapp/src/mcp-fs.ts`
- `webapp/src/polyfills/fs.ts`
- `webapp/src/tic80ctl-commands.ts`

Current bundled extension example:

- `webapp/src/bundled-extensions/tic80ctl-lint-cart-on-lua-write.ts`

---

# The Key Rule

**Do not try to make an arbitrary Node Pi extension run unchanged in the browser.**

Instead:

- port the logic
- keep behavior as close as possible
- swap Node APIs for browser/runtime-aware equivalents
- bundle the result into the webapp

The correct target is:

> browser-safe extension behavior with Pi-compatible runtime semantics

not:

> generic Node extension parity

---

# What Makes an Extension Portable

Good candidates:

- tool event hooks (`tool_call`, `tool_result`)
- input or session lifecycle hooks
- command registration
- status notifications
- lightweight context/system-prompt augmentation
- browser-shell-driven side effects via `pi.exec(...)`

Harder candidates:

- extensions requiring raw Node filesystem semantics
- extensions depending on `child_process`
- extensions depending on npm-installed third-party modules at runtime
- extensions requiring full custom TUI widgets/components
- extensions expecting unrestricted OS access
- extensions using complex provider registration flows

The currently ported example is a good pattern because it mostly needs:

- event hooks
- `pi.exec(...)`
- `ctx.ui.notify(...)`
- access to the active filesystem view

---

# Current Browser Extension Loading Strategy

## High level

We currently support **bundled browser-safe extensions**, not generic dynamic discovery from arbitrary project extension folders.

That means:

1. the extension source lives in `webapp/src/bundled-extensions/`
2. the extension factory is imported directly into the browser bundle
3. its raw source is also imported and written into the browser workspace at:
   - `/workspace/.pi/extensions/...`
4. the browser resource loader returns the extension as part of `getExtensions()`
5. Pi's session builds an `ExtensionRunner` from those loaded extensions

## Why both raw source and runtime import exist

We currently do two different things with the bundled extension:

### 1. Runtime loading
Used so the extension actually executes.

That happens via:

- `webapp/src/bundled-extension-runtime.ts`

which imports the extension factory directly.

### 2. Source installation into workspace
Used so the extension exists at a meaningful project path inside the active Pi filesystem:

- `/workspace/.pi/extensions/tic80ctl-lint-cart-on-lua-write.ts`

That happens via:

- `webapp/src/bundled-extension.ts`

which imports the extension source with `?raw` and writes it into the workspace + browser `fs` polyfill store.

This mirrors the approach used for bundled skills.

---

# The Current Browser Resource Loader

File:

- `webapp/src/browser-resource-loader.ts`

The browser resource loader is currently domain-specific and hardcoded.

It returns:

- the bundled skill from `getSkills()`
- the bundled extension from `getExtensions()`
- empty prompts/themes/agents files
- no extra system prompt

## Important behavior

The resource loader must call `reload()` before session creation so `getExtensions()` returns actual loaded extensions and a runtime object.

Current pattern in `main.ts`:

1. create workspace
2. install bundled resources into workspace
3. create browser resource loader
4. `await resourceLoader.reload()`
5. construct `AgentSession`

Do not skip the reload.

---

# The Current Browser Extension Shim

File:

- `webapp/src/shims/browser-extensions.ts`

This file is crucial.

The webapp aliases Pi's normal extension entrypoint to this browser shim in `vite.config.ts`.
So when `AgentSession` imports from Pi's `extensions/index.js`, it gets the browser version.

## What this shim does now

It provides a minimal browser-compatible surface for Pi's extension system:

- `createExtensionRuntime()`
- `loadExtensionFromFactory()`
- `loadExtensions()`
- `discoverAndLoadExtensions()`
- `wrapRegisteredTool()`
- `wrapRegisteredTools()`
- `defineTool`
- browser `ExtensionRunner`

## Important design choice

The current browser `ExtensionRunner` subclasses Pi's real:

- `PiExtensionRunner`

and overrides:

- `createContext()`
- `createCommandContext()`

so that browser extension contexts get:

- `ctx.fs = workspace.fs`

This is the current browser-only escape hatch that lets a ported extension inspect the active filesystem.

That is currently the most important adaptation for extensions like the TIC-80 lint extension.

---

# `ctx.fs` Is the Critical Browser Port Mechanism

If an extension needs filesystem access in the webapp, it should usually use:

- `ctx.fs`

not Node `fs`, and not the browser polyfill `fs` module directly.

## Why

Because the active Pi filesystem changes over time.

Before TIC-80 startup, the active workspace may be browser-local.
After TIC-80 startup, the active workspace is MCP-backed and points into TIC-80's live filesystem.

`ctx.fs` is the one abstraction that can follow that active workspace view.

This is much more correct than reading some unrelated static host path.

## What `ctx.fs` should be treated as

Conceptually, `ctx.fs` is the active `IFileSystem` from:

- `BrowserWorkspace.fs`

Useful methods expected by browser-ported extensions:

- `resolvePath(base, path)`
- `readFile(path, "utf8")`
- `lstat(path)`
- `readdir(path)`
- optionally `readdirWithFileTypes(path)`

If a new extension needs more than this, stop and inspect the actual FS abstraction before inventing new helpers.

---

# The Ported Extension Pattern

Current bundled extension:

- `webapp/src/bundled-extensions/tic80ctl-lint-cart-on-lua-write.ts`

This is a browser port of the original Node extension.

## The major change from Node version

The Node version used:

- `node:fs/promises`
- `readdir(..., { withFileTypes: true })`
- `lstat(...).mtimeMs`

The browser port instead uses:

- `ctx.fs`
- browser-side recursive traversal helpers
- `Date`-based `mtime` conversion when present
- browser `pi.exec("bash", ...)`

## Behavioral goal

The browser port tries to preserve the original extension behavior 1:1:

- snapshot Lua files before bash tool call
- snapshot again after tool result
- diff changed Lua files
- classify cart vs playtest files
- run `tic80ctl lint-cart` / `tic80ctl lint-playtest-script`
- append lint summary into tool result output
- notify in UI

This is the recommended style for future ports:

> preserve behavior, replace runtime dependencies

---

# Adding Another Extension: Step-by-Step

Use this sequence.

## Step 1: decide whether the extension should be bundled

For the current webapp architecture, assume **yes** unless there is a strong reason otherwise.

Create a bundled source file under:

- `webapp/src/bundled-extensions/<name>.ts`

Use a browser-safe port, not a direct Node copy unless it is already browser-compatible.

## Step 2: port Node-specific parts

Rewrite anything that depends on:

- `node:fs`
- `node:fs/promises`
- `child_process`
- unrestricted OS APIs
- dynamic npm resolution
- jiti/TypeScript runtime loading

Preferred substitutions:

- use `ctx.fs` for filesystem reads/walks
- use `pi.exec(...)` for shell-like operations the browser runtime already supports
- use `ctx.ui.notify(...)` / status methods for UI

## Step 3: create a bundled runtime import shim

Add or update a file like:

- `webapp/src/bundled-extension-runtime.ts`

If only one extension exists, it may simply export the default factory.
If multiple bundled extensions are added, convert this into a registry or explicit exports.

## Step 4: expose raw source into the workspace

Add or update:

- `webapp/src/bundled-extension.ts`

This file should:

1. import the extension source with `?raw`
2. choose a virtual project path under:
   - `/workspace/.pi/extensions/`
3. create a `sourceInfo` using Pi's synthetic source helper
4. provide an installer that writes the source file into:
   - the active workspace FS
   - the browser `fs` polyfill store

Why both?

- workspace FS makes it visible through the active Pi filesystem
- browser `fs` polyfill store keeps Pi-internal path-based reads consistent where needed

## Step 5: add it to the browser resource loader

Update:

- `webapp/src/browser-resource-loader.ts`

In `reload()`:

- construct a fresh browser extension runtime
- load the bundled extension factory with `loadExtensionFromFactory(...)`
- set the correct `sourceInfo`
- populate `extensionsResult`

If adding multiple extensions:

- load all bundled factories
- return them all in `extensionsResult.extensions`

## Step 6: install the raw extension file into the workspace before session creation

Update:

- `webapp/src/main.ts`

Wherever the active Pi workspace is created, install bundled extension source files before session creation.

Current relevant location:

- `initializePiFromActiveTic80()`

Make sure the installer runs for every new active workspace instance.

## Step 7: call `resourceLoader.reload()` before `AgentSession` creation

If you forget this, the extension will exist as a file but will not be loaded into Pi's runtime.

## Step 8: validate with a build

Run:

```bash
cd webapp
pnpm build
```

Then test the specific extension behavior in the browser.

---

# Files You Usually Need To Touch

For a new bundled browser extension, expect to touch:

- `webapp/src/bundled-extensions/<extension>.ts`
- `webapp/src/bundled-extension.ts`
- `webapp/src/bundled-extension-runtime.ts`
- `webapp/src/browser-resource-loader.ts`
- `webapp/src/shims/browser-extensions.ts`
- `webapp/src/main.ts`

You may also need to touch:

- `webapp/src/browser-workspace.ts`
- `webapp/src/mcp-fs.ts`
- `webapp/src/polyfills/fs.ts`

if the new extension needs additional browser-runtime guarantees.

---

# Current Initialization Flow

The relevant extension load flow is currently:

1. TIC-80 session starts
2. webapp creates a new `BrowserWorkspace` over MCP FS
3. bundled skill files are installed
4. bundled extension source file is installed
5. browser resource loader is created
6. `resourceLoader.reload()` loads the extension factory and runtime
7. `AgentSession` is created
8. Pi's `AgentSession` builds an `ExtensionRunner`
9. the browser `ExtensionRunner` injects `ctx.fs`
10. extension hooks begin receiving events

This means extension support is currently tied to active Pi-on-TIC-80 initialization, not a generic idle Pi state.

---

# Important Constraints And Caveats

## 1. This is not generic extension discovery yet

Do not assume arbitrary files under `.pi/extensions/` are auto-loaded.
Current support is for bundled browser-safe extensions loaded explicitly by the browser resource loader.

## 2. `ctx.fs` is a browser-only addition

This is not standard full-Pi extension API behavior. It is a webapp-specific bridge.
Do not remove it casually.

If a future refactor changes how browser extensions access the active filesystem, audit all bundled extensions that depend on it.

## 3. `pi.exec(...)` is browser-shell-backed

In this webapp, `pi.exec(...)` is currently implemented through browser workspace shell execution, not a true OS subprocess layer.
That is good enough for this domain, but do not overstate it.

## 4. The active workspace may change

Because Pi attaches to TIC-80 MCP FS, any extension that reads or walks files must assume the current workspace view matters.
That is why `ctx.fs` is the right abstraction.

## 5. Browser extension support is still partial

Current work is enough for event-driven, browser-safe extensions, especially:

- commands
- tool hooks
- notifications
- runtime shell integration

It is not proof that every Pi extension can be made to work unchanged.

---

# Testing Checklist For A New Ported Extension

After adding a new bundled extension:

## Build-level

- `cd webapp && pnpm build`

## Resource-level

Confirm the raw extension source is installed at:

- `/workspace/.pi/extensions/<name>.ts`

from the active Pi workspace point of view.

## Runtime-level

Confirm `resourceLoader.reload()` successfully returns the extension in:

- `getExtensions().extensions`

## Behavior-level

Exercise the exact extension behavior it claims to implement.

For the current lint extension, that means:

- start TIC-80 session
- enable Pi
- use Pi to `write` or `edit` a `.lua` file
- confirm lint annotations are appended to tool results
- confirm UI notifications appear
- use Pi `bash` to modify `.lua` files and confirm snapshot-diff linting runs

## Failure-level

Inspect:

- browser console
- Pi status text
- extension error output in Pi if surfaced
- `resourceLoader.getExtensions().errors`

---

# Common Failure Modes

## Extension file exists but does not run

Likely causes:

- forgot `await resourceLoader.reload()`
- resource loader did not include extension in `getExtensions()`
- extension factory import path wrong
- browser shim not exporting the runtime/load functions Pi expects

## Extension runs but cannot read files

Likely causes:

- extension still uses Node FS imports
- `ctx.fs` missing from browser `ExtensionRunner`
- extension is reading the wrong path style
- workspace file was not installed into current active FS

## Extension runs but shell command behavior differs

Likely causes:

- browser `pi.exec(...)` semantics differ from Node subprocess expectations
- shell builtin assumptions do not hold in just-bash
- command availability checks are too Node-specific

The current lint extension already includes a browser-conscious availability check pattern. Reuse that thinking.

## Extension source visible in workspace but not listed as loaded resource

Likely causes:

- raw source installation was done
- but browser resource loader did not create/load the extension object

Remember: visible file and loaded runtime extension are separate concerns.

---

# Recommended Style For Future Ports

When porting a new extension:

- keep the event structure the same if possible
- keep user-visible behavior the same if possible
- keep result formatting/messages close to original
- replace runtime-dependent pieces surgically
- prefer browser-native abstractions over emulating all of Node

Good pattern:

- port the logic
- keep the extension name/path recognizable
- document browser-only substitutions inline

Bad pattern:

- rewrite behavior so heavily that the original extension intent is lost
- hide runtime assumptions in vague helpers
- mix generic webapp changes with extension-specific logic unless necessary

---

# When Not To Use This Pattern

Do **not** use this bundled-extension pattern if the goal is:

- generic end-user extension discovery from arbitrary `.pi/extensions/`
- npm-backed extension ecosystems in-browser
- true Node-compatible dynamic extension loading
- unrestricted CLI extension parity

That is a different project.

This skill is specifically for:

> adding a new extension to the TIC-80 webapp Pi in the same style as the current bundled browser extension support

---

# Minimal Handoff Summary

If you only remember one thing, remember this:

> In the webapp, a new Pi extension is added by bundling a browser-safe port under `webapp/src/bundled-extensions/`, loading it explicitly in `browser-resource-loader.ts`, installing its raw source into `/workspace/.pi/extensions/...`, and making sure browser extension contexts get `ctx.fs` for active filesystem access.

That is the current pattern.
