# Browser-Hosted `tic80ctl` Host Wiring

## Goal
Provide a browser host/demo layer for the wasm `tic80ctl` runtime so the web build can drive a TIC-80 Emscripten target over MCP `postMessage`.

## Scope
- Static webapp host assets under `build/webapp/`
- Browser smoke coverage under `tools/mcp/`
- CI wiring for the browser-hosted demo flow

Out of scope:
- The native Unix `tic80ctl` supervisor
- TIC-80 MCP transport internals in `src/system/sdl/main.c`
- Browser runtime/compiler changes in `build/tools/tic80ctl*`

## Host Assets
- `build/webapp/tic80ctl-browser-demo.html`
  - A host page with buttons for `start`/`status`/`run`/`stop`
  - Supports both owned iframe and owned popup TIC-80 targets
- `build/webapp/tic80ctl-browser-host.mjs`
  - Resolves the browser wasm `tic80ctl` factory from `window.createTic80CtlBrowser()`
  - Falls back to importing `./tic80ctl-browser.js` or `./tic80ctl-browser.mjs`
  - Provides a mock controller path via `?mock=1`
  - Coordinates owned iframe/popup lifecycle and explicit `bindTarget(...)`

The host module expects the runtime-facing API below once the wasm controller lands:

```js
const controller = await createTic80CtlBrowser();
await controller.bindTarget(windowHandle, {
  kind: "iframe" | "popup",
  owned: true,
  origin: "http://localhost:8000",
});
await controller.run(["start"]);
await controller.run(["run"]);
await controller.status();
await controller.stop();
await controller.dispose();
```

## Demo Usage
Serve the webapp directory and open:

```bash
cd build/webapp
python3 -m http.server 8000
```

Then open:

- `http://localhost:8000/tic80ctl-browser-demo.html`
- `http://localhost:8000/tic80ctl-browser-demo.html?mock=1`

`?mock=1` exercises the host lifecycle without depending on the wasm `tic80ctl` artifact.

## Smoke Coverage
`tools/mcp/tic80ctl_browser_host_smoke.sh` verifies:
- demo files exist and parse
- the host module exports the expected coordinator/mock pieces
- iframe ownership cleanup fires on session replacement
- popup ownership cleanup fires on `stop`
- the coordinator issues `bindTarget`, `run(["start"])`, and `run(["run"])`

## CI Wiring
Run the host smoke after the existing web MCP smoke in both:
- `.github/workflows/build.yml`
- `.github/workflows/webapp.yml`

This keeps browser-hosted `tic80ctl` wiring checked in CI even before the real wasm controller artifact is produced.
