# TIC-80 Emscripten MCP postMessage Transport

## Goal
Provide a browser-native MCP transport for Emscripten builds while preserving the native Unix `--mcp` stdio transport unchanged.

## Build Option
- CMake option: `BUILD_MCP_POSTMESSAGE`
- Default: `ON` for Emscripten, `OFF` elsewhere
- Scope: Emscripten-only web artifacts

When enabled, the SDL Emscripten target now:
- defines `BUILD_MCP_POSTMESSAGE=1`
- keeps the existing `build/html/prejs.js` injection path
- compiles `src/system/sdl/main.c` with the browser transport enabled

## Browser Transport Contract
The browser bridge in `build/html/prejs.js`:
- accepts raw JSON-RPC 2.0 object messages from a bound browser controller
- serializes accepted requests and queues them for native consumption
- rejects oversized requests locally with JSON-RPC error `-32600`
- sends raw JSON-RPC response objects back through the active browser transport

Browser transports currently split by topology:
- iframe / parent window:
  - direct `window.postMessage` carrying raw JSON-RPC objects
- popup / opener window:
  - a one-time `window.postMessage` handshake that transfers a `MessagePort`
  - all subsequent MCP JSON-RPC traffic runs over that dedicated `MessagePort`
  - the popup validates the transferred-channel handshake with:
    - `window.opener`
    - a per-popup token in `tic80ctl_popup_token`
    - an optional exact opener origin in `tic80ctl_popup_origin`

The native MCP loop in `src/system/sdl/main.c`:
- is transport-agnostic at the request-handler layer
- continues to use `stdin` and `stdout` on native targets
- polls the browser queue and emits responses through the bridge on Emscripten builds

## Native Behavior
- Unix targets are unchanged: `tic80 --mcp` still reads JSON-RPC from `stdin` and writes responses to `stdout`.
- Browser builds do not require `--mcp`; the postMessage bridge is active whenever `BUILD_MCP_POSTMESSAGE=ON`.

## Local Verification
Example web build:

```bash
cd build
emcmake cmake -DBUILD_SDLGPU=On -DBUILD_STATIC=On -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_ALL=On -DBUILD_MCP_POSTMESSAGE=On .. --fresh
cmake --build . --parallel
../tools/mcp/web_postmessage_smoke.sh bin/tic80.js
```

The smoke script verifies:
- `build/html/prejs.js` still contains the JSON-RPC browser bridge and exported C interop functions
- the generated `tic80.js` contains the emitted browser bridge code

Popup-specific browser verification:

```bash
../tools/mcp/web_popup_message_channel_smoke.sh webapp
```

This verifies the browser `tic80ctl` runtime:
- opens popup targets with `tic80ctl_popup_token` and `tic80ctl_popup_origin`
- establishes a dedicated `MessageChannel`
- sends `initialize` and `tools/call` over the `MessagePort` instead of the global window bus
