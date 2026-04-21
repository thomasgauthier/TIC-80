#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
INPUT_PATH="${1:-$ROOT_DIR/build/webapp/tic80ctl-browser.js}"

if [ -d "$INPUT_PATH" ]; then
  JS_PATH="$INPUT_PATH/tic80ctl-browser.js"
else
  JS_PATH="$INPUT_PATH"
fi

if [ ! -f "$JS_PATH" ]; then
  echo "missing browser controller asset: $JS_PATH" >&2
  exit 1
fi

grep -q 'MessageChannel' "$JS_PATH"
grep -q 'tic80ctl_popup_token' "$JS_PATH"
grep -q 'tic80ctl_popup_origin' "$JS_PATH"

JS_PATH="$JS_PATH" node --input-type=module <<'NODE'
import path from "node:path";
import { pathToFileURL } from "node:url";

function createFakePort() {
  const listeners = new Set();

  return {
    closed: false,
    other: null,
    onmessage: null,
    addEventListener(type, handler) {
      if (type === "message") {
        listeners.add(handler);
      }
    },
    removeEventListener(type, handler) {
      if (type === "message") {
        listeners.delete(handler);
      }
    },
    start() {},
    close() {
      this.closed = true;
    },
    postMessage(data) {
      queueMicrotask(() => {
        if (!this.other || this.other.closed) {
          return;
        }

        const event = { data };
        if (typeof this.other.onmessage === "function") {
          this.other.onmessage(event);
        }
        for (const handler of listenersFor(this.other)) {
          handler(event);
        }
      });
    },
    _listeners: listeners,
  };
}

function listenersFor(port) {
  return port && port._listeners ? port._listeners : [];
}

function createFakeMessageChannel() {
  const port1 = createFakePort();
  const port2 = createFakePort();
  port1.other = port2;
  port2.other = port1;
  return { port1, port2 };
}

const jsPath = process.env.JS_PATH;
const moduleUrl = pathToFileURL(path.resolve(jsPath)).href;
const windowListeners = new Set();

let openCall = null;
let popupWindowJsonRpcCount = 0;
let popupHandshakeCount = 0;
const popupPortMessages = [];

const popupWindow = {
  closed: false,
  postMessage(payload, _origin, transferList) {
    if (payload && payload.tic80ctlBridge === "tic80ctl-popup-port-v1") {
      popupHandshakeCount += 1;
      if (!payload.token) {
        throw new Error("popup handshake token missing");
      }

      const popupPort = transferList && transferList[0];
      if (!popupPort) {
        throw new Error("popup handshake did not transfer a MessagePort");
      }

      popupPort.onmessage = (event) => {
        const message = event.data;
        popupPortMessages.push(message);

        if (message.method === "initialize") {
          popupPort.postMessage({
            jsonrpc: "2.0",
            id: message.id,
            result: {
              protocolVersion: "2025-03-26",
            },
          });
          return;
        }

        if (message.method === "tools/call") {
          popupPort.postMessage({
            jsonrpc: "2.0",
            id: message.id,
            result: {
              content: [{ type: "text", text: message.params.arguments.command || "ok" }],
              isError: false,
            },
          });
        }
      };

      popupPort.postMessage({
        tic80ctlBridge: "tic80ctl-popup-port-v1",
        type: "ready",
        token: payload.token,
      });
      return;
    }

    if (payload && payload.jsonrpc === "2.0") {
      popupWindowJsonRpcCount += 1;
      throw new Error("popup JSON-RPC should not be sent over window.postMessage after channel setup");
    }
  },
  close() {
    this.closed = true;
  },
};

globalThis.MessageChannel = class MessageChannel {
  constructor() {
    const channel = createFakeMessageChannel();
    this.port1 = channel.port1;
    this.port2 = channel.port2;
  }
};

globalThis.window = {
  location: {
    origin: "http://localhost:8000",
    href: "http://localhost:8000/tic80ctl-browser-demo.html",
  },
  crypto: {
    getRandomValues(buffer) {
      for (let index = 0; index < buffer.length; index += 1) {
        buffer[index] = index + 1;
      }
      return buffer;
    },
  },
  addEventListener(type, handler) {
    if (type === "message") {
      windowListeners.add(handler);
    }
  },
  removeEventListener(type, handler) {
    if (type === "message") {
      windowListeners.delete(handler);
    }
  },
  setTimeout,
  clearTimeout,
  open(url, name, features) {
    openCall = { url, name, features };
    
    // Simulate the popup sending the request_port message to the opener
    setTimeout(() => {
      for (const listener of windowListeners) {
        listener({
          source: popupWindow,
          data: {
            tic80ctlBridge: "tic80ctl-popup-port-v1",
            type: "request_port",
            token: "popup-token-fixed"
          }
        });
      }
    }, 50);

    return popupWindow;
  },
};

const { createTic80CtlBrowser } = await import(moduleUrl);

const controller = await createTic80CtlBrowser({
  coreFactory: async () => {
    return {
      async ccall(name, returnType, argTypes, args, opts) {
        const input = JSON.parse(args[0]);
        const cmd = input.argv[0] || "";
        
        if (cmd === "start") {
          const res = await this.tic80ctlBrowserHost.invoke(JSON.stringify({ op: "start" }));
          return JSON.stringify({ stdout: JSON.stringify(res), stderr: "", exit_code: 0 });
        }
        
        if (cmd === "run") {
          const res = await this.tic80ctlBrowserHost.invoke(JSON.stringify({ op: "tool", tool: "run_command", arguments: { command: "run" } }));
          return JSON.stringify({ stdout: "run", stderr: "", exit_code: 0, json: res });
        }

        return JSON.stringify({ stdout: "mock", stderr: "", exit_code: 0 });
      }
    };
  },
  createPopupToken: () => "popup-token-fixed",
});

await controller.openPopupTarget("./index.html", {
  origin: "http://localhost:8000",
});

const startResult = await controller.run(["start", "--json"]);
const runResult = await controller.run(["run"]);
const statusResult = await controller.status();
await controller.stop();

if (!openCall) {
  throw new Error("window.open was not called");
}

if (!openCall.url.includes("tic80ctl_popup_token=popup-token-fixed")) {
  throw new Error(`popup URL missing token: ${openCall.url}`);
}

if (!openCall.url.includes("tic80ctl_popup_origin=http%3A%2F%2Flocalhost%3A8000")) {
  throw new Error(`popup URL missing controller origin: ${openCall.url}`);
}

if (popupHandshakeCount !== 1) {
  throw new Error(`expected 1 popup handshake, got ${popupHandshakeCount}`);
}

if (popupWindowJsonRpcCount !== 0) {
  throw new Error(`expected 0 popup window JSON-RPC messages, got ${popupWindowJsonRpcCount}`);
}

if (!startResult.json || startResult.json.initialized !== true) {
  throw new Error(`unexpected popup start payload: ${JSON.stringify(startResult)}`);
}

if (runResult.exitCode !== 0 || runResult.stdout.trim() !== "run") {
  throw new Error(`unexpected popup run result: ${JSON.stringify(runResult)}`);
}

if (!statusResult || statusResult.targetKind !== "popup" || statusResult.initialized !== true) {
  throw new Error(`unexpected popup status: ${JSON.stringify(statusResult)}`);
}

const initializeMessages = popupPortMessages.filter((message) => message && message.method === "initialize");
const toolCallMessages = popupPortMessages.filter((message) => message && message.method === "tools/call");

if (initializeMessages.length !== 1) {
  throw new Error(`expected 1 initialize over MessagePort, got ${initializeMessages.length}`);
}

if (toolCallMessages.length !== 1) {
  throw new Error(`expected 1 tools/call over MessagePort, got ${toolCallMessages.length}`);
}

console.log("Popup MessageChannel smoke test passed.");
console.log(`js: ${jsPath}`);
NODE
