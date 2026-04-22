# BUILD.md

This document describes the known-good steps to make the **Vite app in `./webapp/`** functional and servable.

## Important path distinction

This file is about:
- `./webapp/` — the Vite app

It is **not** about:
- `./build/webapp/`

For this workflow, the Vite app serves TIC-80 runtime assets from:
- `webapp/public/tic80-runtime/`

Those runtime assets are synced from:
1. `TIC80_RUNTIME_DIR` (if set)
2. `../build-web-tic80ctl/bin`
3. `../build/webapp`

The known-good source is:
- `../build-web-tic80ctl/bin`

## Prerequisites

- `pnpm`
- Emscripten (`emcmake`)
- local `pi-mono` checkout at:
  - `/workspace/pi-mono`

## 1. Build the TIC-80 browser runtime

From repo root:

```bash
cd /workspace/tic80/TIC-80
mkdir -p build-web-tic80ctl
cd build-web-tic80ctl
emcmake cmake \
  -DBUILD_SDLGPU=On \
  -DBUILD_STATIC=On \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_WITH_ALL=On \
  -DBUILD_MCP_POSTMESSAGE=On \
  ..
EM_CONFIG=$PWD/.tmp_emscripten_config EM_CACHE=$PWD/.emcache \
  cmake --build . --parallel --target tic80 tic80ctl-browser-core
```

Expected outputs:
- `build-web-tic80ctl/bin/tic80.js`
- `build-web-tic80ctl/bin/tic80.wasm`
- `build-web-tic80ctl/bin/tic80ctl-browser-core.js`
- `build-web-tic80ctl/bin/tic80ctl-browser-core.wasm`

## 2. Install webapp dependencies

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm install
```

## 3. Run the Vite app in dev mode

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm dev
```

What this does automatically:
1. links local `pi-mono` packages
2. syncs TIC-80 runtime assets into `webapp/public/tic80-runtime/`
3. starts Vite

Default host:
- `0.0.0.0`

If you want a specific port, for example `4173`:

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm dev -- --port 4173
```

## 4. Build the Vite app for production

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm build
pnpm verify:dist
```

This also performs the runtime sync first.

## 5. Preview the production build

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm preview
```

To use port `4173`:

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm preview -- --port 4173
```

Then verify:

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm verify:preview
```

## 6. Quick manual verification checklist

1. Open the Vite app in the browser
2. Confirm the Pi terminal renders
3. Start **iframe** session and confirm TIC-80 appears in the embedded frame
4. Start **popup** session and confirm TIC-80 opens in a popup
5. Confirm Pi terminal filesystem switches to TIC-80 MCP-backed FS after successful start
6. Confirm `tic80ctl cmd ls` works

## Troubleshooting

### Runtime assets not found

If sync fails, rebuild the browser runtime:

```bash
cd /workspace/tic80/TIC-80/build-web-tic80ctl
EM_CONFIG=$PWD/.tmp_emscripten_config EM_CACHE=$PWD/.emcache \
  cmake --build . --parallel --target tic80 tic80ctl-browser-core
```

### Browser serves stale runtime

If behavior looks wrong after a runtime change:
- hard refresh
- try an incognito/private window
- unregister service workers / clear site data

### Important implementation note

The Vite app owns its runtime page template at:
- `webapp/runtime/index.html`

The sync script copies that into:
- `webapp/public/tic80-runtime/index.html`

So if iframe runtime boot behavior needs adjustment for the Vite app, change:
- `webapp/runtime/index.html`

not a generated build directory.
