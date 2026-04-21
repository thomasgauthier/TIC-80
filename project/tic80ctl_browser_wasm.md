# Browser-Hosted `tic80ctl` Assets

## Goal
Provide browser-facing host assets that can exercise the Emscripten MCP `postMessage` transport using a `tic80ctl`-shaped API, without depending on the native Unix supervisor model.

## Assets
- `build/webapp/tic80ctl-browser.js`
- `build/webapp/tic80ctl-demo.html`
- `tools/mcp/web_tic80ctl_assets_smoke.sh`

## `tic80ctl-browser.js`
`build/webapp/tic80ctl-browser.js` exports:

```js
createTic80CtlBrowser(options?)
```

The returned controller keeps all state in memory and exposes:
- `bindTarget(windowHandle, { kind, owned, origin, removeOwnedTarget? })`
- `bindIframe(iframe, { owned, origin, removeOwnedTarget? })`
- `openPopupTarget(url, { origin, name, features })`
- `initialize()`
- `request(method, params)`
- `callTool(name, argumentsObject)`
- `run(argv, runOptions?)`
- `status()`
- `stop()`
- `dispose()`

Current command coverage in `run(argv)` is intentionally transport-focused:
- `start`
- `status`
- `stop`
- `cmd`
- `load`
- `run`
- `eval`
- `screenshot`
- `tools`

That keeps the browser asset useful now, while the future wasm `tic80ctl` runtime can replace or absorb this parser once full command parity lands.

## Session Model
- Browser sessions are ephemeral and exist only in the hosting page memory.
- `start` means “initialize MCP against the currently bound TIC-80 target”.
- `status` reports binding and initialization state.
- `stop` closes/removes the target when the controller owns it; otherwise it detaches.

Transport split:
- iframe targets keep using the parent-window raw `postMessage` MCP bridge
- popup targets now use a tokenized `MessageChannel` handshake and then carry MCP JSON-RPC over the dedicated `MessagePort`

## Demo Page
`build/webapp/tic80ctl-demo.html` hosts a TIC-80 iframe and exercises the browser controller:
- mounts an iframe pointing at `./index.html`
- issues `tic80ctl start`
- sends `run` and arbitrary `cmd` requests
- shows raw `tools/list`
- supports a popup target button backed by the `MessageChannel` popup transport

The iframe path remains on the simple parent-window bridge. Popup control now requires the dedicated handshake path because modern browsers are stricter about opener/window messaging than the iframe case.

## Smoke Verification
Run:

```bash
tools/mcp/web_tic80ctl_assets_smoke.sh
```

The smoke script verifies:
- the browser controller module imports under Node ESM
- `createTic80CtlBrowser` is exported
- the demo page references the controller and key commands
