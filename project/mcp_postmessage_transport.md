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
- accepts only raw JSON-RPC 2.0 object messages from a bound browser controller window
- serializes accepted requests and queues them for native consumption
- rejects oversized requests locally with JSON-RPC error `-32600`
- sends raw JSON-RPC response objects back to the bound controller with `postMessage`

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
