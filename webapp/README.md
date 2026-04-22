# TIC-80 combined browser host + Pi web TUI

This Vite app adds a new browser page that combines:

- the existing owned TIC-80 iframe/popup startup flow
- the public Pi Path A browser TUI

The Pi panel is a plain browser TUI embed only. It does **not** expose TIC-80 tools through chat.

## Local package linking

This app is configured for local Path A development against `/workspace/pi-mono`.

The build/dev scripts automatically run:

```bash
pnpm run link:pi-mono
```

That links these local packages into `webapp/node_modules`:

- `/workspace/pi-mono/packages/ai`
- `/workspace/pi-mono/packages/tui`
- `/workspace/pi-mono/packages/coding-agent`

## TIC-80 runtime assets

Before the page can launch TIC-80 in an iframe or popup, the browser runtime assets must exist.

The sync script looks in this order:

1. `TIC80_RUNTIME_DIR`
2. `../build-web-tic80ctl/bin`
3. `../build/webapp`

The scripts copy the runtime into `webapp/public/tic80-runtime/` before dev/build.

## Install

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm install
```

## Run in dev mode

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm dev
```

## Production build

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm build
pnpm verify:dist
```

## Preview the production build

In one shell:

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm preview
```

In another shell:

```bash
cd /workspace/tic80/TIC-80/webapp
pnpm verify:preview
```

## Manual verification checklist

1. Open the page.
2. Confirm the Pi terminal renders on the right and shows the browser TUI startup message.
3. With provider set to `mock`, submit a prompt and confirm the Pi panel responds.
4. Click **Start owned iframe session** and confirm TIC-80 appears in the embedded frame.
5. Click **Start owned popup session** and confirm TIC-80 opens in a popup.
6. Use **Status**, **Run**, and **Stop** to confirm the host controls still work.
