#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WEBAPP_DIR="${1:-$ROOT_DIR/build/webapp}"
HOST_MODULE="$WEBAPP_DIR/tic80ctl-browser-host.mjs"
DEMO_HTML="$WEBAPP_DIR/tic80ctl-browser-demo.html"

if [ ! -f "$HOST_MODULE" ]; then
  echo "missing host module: $HOST_MODULE" >&2
  exit 1
fi

if [ ! -f "$DEMO_HTML" ]; then
  echo "missing demo html: $DEMO_HTML" >&2
  exit 1
fi

node --check "$HOST_MODULE"

grep -q 'Start owned iframe session' "$DEMO_HTML"
grep -q 'Start owned popup session' "$DEMO_HTML"
grep -q 'bootTic80CtlBrowserDemo' "$DEMO_HTML"
grep -q 'createBrowserTargetCoordinator' "$HOST_MODULE"
grep -q 'createMockTic80CtlBrowser' "$HOST_MODULE"
grep -q 'window.open' "$HOST_MODULE"
grep -q 'bindTarget' "$HOST_MODULE"
grep -q 'runCommand' "$HOST_MODULE"

HOST_MODULE_PATH="$HOST_MODULE" node --input-type=module <<'NODE'
import path from "node:path";
import { pathToFileURL } from "node:url";

const hostModulePath = process.env.HOST_MODULE_PATH;
const mod = await import(pathToFileURL(path.resolve(hostModulePath)).href);

const bindCalls = [];
const runCalls = [];
const removedKinds = [];
const closedKinds = [];

const controller = {
  async bindTarget(handle, options) {
    bindCalls.push({ handle, options });
  },
  async run(argv) {
    runCalls.push(argv.slice());
    return {
      stdout: argv.join(" "),
      stderr: "",
      exitCode: 0,
      json: { argv: argv.slice() },
    };
  },
  async status() {
    const lastBind = bindCalls[bindCalls.length - 1];
    return {
      bound: bindCalls.length > 0,
      initialized: bindCalls.length > 0,
      targetKind: lastBind ? lastBind.options.kind : null,
      owned: lastBind ? !!lastBind.options.owned : false,
      closed: false,
      origin: lastBind ? lastBind.options.origin : null,
    };
  },
  async stop() {},
  async dispose() {},
};

const coordinator = mod.createBrowserTargetCoordinator({
  controllerFactory: async () => controller,
  openIframeTarget: async () => ({
    owned: true,
    windowHandle: { kind: "iframe-window", closed: false },
    origin: "http://localhost:8000",
    iframe: { remove() { removedKinds.push("iframe-remove"); } },
  }),
  openPopupTarget: async () => ({
    owned: true,
    windowHandle: { kind: "popup-window", closed: false },
    origin: "http://localhost:8000",
    popup: { closed: false, close() { closedKinds.push("popup-close"); this.closed = true; } },
  }),
  removeIframeTarget(target) {
    removedKinds.push(target.kind);
  },
  closePopupTarget(target) {
    closedKinds.push(target.kind);
  },
});

await coordinator.start("iframe");
await coordinator.runCommand(["run"]);
const iframeStatus = await coordinator.status();
if (iframeStatus.targetKind !== "iframe") {
  throw new Error(`expected iframe status, got ${JSON.stringify(iframeStatus)}`);
}

await coordinator.start("popup");
const popupStatus = await coordinator.status();
if (popupStatus.targetKind !== "popup") {
  throw new Error(`expected popup status, got ${JSON.stringify(popupStatus)}`);
}

await coordinator.stop();

if (bindCalls.length !== 2) {
  throw new Error(`expected 2 bindTarget calls, got ${bindCalls.length}`);
}

if (runCalls.length !== 3) {
  throw new Error(`expected 3 run calls, got ${runCalls.length}`);
}

if (!removedKinds.includes("iframe")) {
  throw new Error(`expected iframe cleanup, got ${JSON.stringify(removedKinds)}`);
}

if (!closedKinds.includes("popup")) {
  throw new Error(`expected popup cleanup, got ${JSON.stringify(closedKinds)}`);
}
NODE

echo "Browser tic80ctl host smoke test passed."
echo "webapp: $WEBAPP_DIR"
