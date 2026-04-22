import "./style.css";

import {
  BrowserCodingAgentTUI,
  createBrowserMockModel,
  createOpenAICompatibleModel,
} from "@mariozechner/pi-coding-agent/browser";
import { WebTerminal } from "@mariozechner/pi-tui/browser";

import {
  createBrowserTargetCoordinator,
  createIframeTargetHost,
  createPopupTargetHost,
} from "../../build/webapp/tic80ctl-browser-host.mjs";
import { createTic80CtlBrowser } from "../../build/webapp/tic80ctl-browser.js";

function requireElement<T extends HTMLElement>(id: string, ctor: new (...args: never[]) => T): T {
  const element = document.getElementById(id);
  if (!(element instanceof ctor)) {
    throw new Error(`Missing #${id}`);
  }
  return element;
}

type Provider = "mock" | "openai" | "custom-openai";

type PiConfig = {
  provider: Provider;
  modelId: string;
  baseUrl: string;
  apiKey: string;
};

const PI_STORAGE_KEY = "tic80-pi-browser-tui-config";
const DEFAULTS: Record<Provider, Pick<PiConfig, "modelId" | "baseUrl">> = {
  mock: {
    modelId: "mock-llm",
    baseUrl: "browser://mock",
  },
  openai: {
    modelId: "gpt-4.1-mini",
    baseUrl: "https://api.openai.com/v1",
  },
  "custom-openai": {
    modelId: "qwen2.5-coder:latest",
    baseUrl: "http://localhost:11434/v1",
  },
};

const appBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const runtimeBaseUrl = new URL("tic80-runtime/", appBaseUrl);

function runtimeUrl(fileName: string): string {
  return new URL(fileName, runtimeBaseUrl).toString();
}

function normalizePiConfig(config: PiConfig): PiConfig {
  const defaults = DEFAULTS[config.provider];
  return {
    provider: config.provider,
    modelId: config.modelId.trim() || defaults.modelId,
    baseUrl: (config.baseUrl.trim() || defaults.baseUrl).replace(/\/$/, ""),
    apiKey: config.apiKey.trim(),
  };
}

function loadStoredPiConfig(): PiConfig {
  try {
    const raw = window.localStorage.getItem(PI_STORAGE_KEY);
    if (!raw) {
      return normalizePiConfig({
        provider: "mock",
        modelId: DEFAULTS.mock.modelId,
        baseUrl: DEFAULTS.mock.baseUrl,
        apiKey: "",
      });
    }

    const parsed = JSON.parse(raw) as Partial<PiConfig>;
    if (
      parsed.provider !== "mock" &&
      parsed.provider !== "openai" &&
      parsed.provider !== "custom-openai"
    ) {
      throw new Error("Unsupported provider");
    }

    return normalizePiConfig({
      provider: parsed.provider,
      modelId: typeof parsed.modelId === "string" ? parsed.modelId : "",
      baseUrl: typeof parsed.baseUrl === "string" ? parsed.baseUrl : "",
      apiKey: typeof parsed.apiKey === "string" ? parsed.apiKey : "",
    });
  } catch {
    return normalizePiConfig({
      provider: "mock",
      modelId: DEFAULTS.mock.modelId,
      baseUrl: DEFAULTS.mock.baseUrl,
      apiKey: "",
    });
  }
}

function parseCommandArgs(raw: string): string[] {
  return (raw.match(/(?:[^\s"']+|"[^"]*"|'[^']*')+/g) || []).map((arg) =>
    arg.replace(/^"(.*)"$/, "$1").replace(/^'(.*)'$/, "$1"),
  );
}

const iframeHost = requireElement("iframe-host", HTMLElement);
const hostStatus = requireElement("status", HTMLElement);
const hostLog = requireElement("log", HTMLTextAreaElement);
const startIframeButton = requireElement("start-iframe", HTMLButtonElement);
const startPopupButton = requireElement("start-popup", HTMLButtonElement);
const statusButton = requireElement("status-button", HTMLButtonElement);
const runButton = requireElement("run-button", HTMLButtonElement);
const stopButton = requireElement("stop-button", HTMLButtonElement);
const cmdInput = requireElement("cmd-input", HTMLInputElement);
const cmdButton = requireElement("cmd-button", HTMLButtonElement);

const piTerminalElement = requireElement("pi-terminal", HTMLElement);
const piProviderSelect = requireElement("pi-provider", HTMLSelectElement);
const piModelInput = requireElement("pi-model", HTMLInputElement);
const piEndpointInput = requireElement("pi-endpoint", HTMLInputElement);
const piApiKeyInput = requireElement("pi-api-key", HTMLInputElement);
const piApplyButton = requireElement("pi-apply", HTMLButtonElement);
const piStatus = requireElement("pi-status", HTMLElement);

function writeHostLog(message: string): void {
  const line = `[${new Date().toISOString()}] ${message}`;
  hostLog.value = hostLog.value ? `${hostLog.value}\n${line}` : line;
  hostLog.scrollTop = hostLog.scrollHeight;
}

const targetUrl = runtimeUrl("index.html");
const coordinator = createBrowserTargetCoordinator({
  controllerFactory: async () =>
    createTic80CtlBrowser({
      coreModulePath: runtimeUrl("tic80ctl-browser-core.js"),
    }),
  openIframeTarget: createIframeTargetHost({
    documentObject: document,
    iframeHost,
    targetUrl,
    parentOrigin: window.location.origin,
  }),
  openPopupTarget: createPopupTargetHost({
    windowObject: window,
    targetUrl,
    parentOrigin: window.location.origin,
  }),
  removeIframeTarget(currentTarget: { iframe?: HTMLIFrameElement | null }) {
    if (currentTarget.iframe?.parentNode) {
      currentTarget.iframe.parentNode.removeChild(currentTarget.iframe);
    }
  },
  closePopupTarget(currentTarget: { popup?: Window | null }) {
    if (currentTarget.popup && !currentTarget.popup.closed) {
      currentTarget.popup.close();
    }
  },
  log: writeHostLog,
});

async function refreshHostStatus(): Promise<void> {
  hostStatus.textContent = JSON.stringify(await coordinator.status(), null, 2);
}

async function runHostAction(label: string, callback: () => Promise<unknown>): Promise<void> {
  try {
    const result = await callback();
    writeHostLog(`${label}: ${typeof result === "object" ? JSON.stringify(result) : String(result)}`);
  } catch (error) {
    writeHostLog(`${label} failed: ${error instanceof Error ? error.message : String(error)}`);
  }
  await refreshHostStatus();
}

startIframeButton.addEventListener("click", () => {
  void runHostAction("start iframe", () => coordinator.start("iframe"));
});

startPopupButton.addEventListener("click", () => {
  void runHostAction("start popup", () => coordinator.start("popup"));
});

statusButton.addEventListener("click", () => {
  void runHostAction("status", () => coordinator.status());
});

runButton.addEventListener("click", () => {
  void runHostAction("run", () => coordinator.runCommand(["run"]));
});

stopButton.addEventListener("click", () => {
  void runHostAction("stop", () => coordinator.stop());
});

cmdButton.addEventListener("click", () => {
  const args = parseCommandArgs(cmdInput.value.trim());
  if (args.length === 0) {
    return;
  }
  void runHostAction(`run ${args[0]}`, () => coordinator.runCommand(args));
});

cmdInput.addEventListener("keydown", (event) => {
  if (event.key === "Enter") {
    cmdButton.click();
  }
});

let currentPiConfig = loadStoredPiConfig();
let activePiApp: BrowserCodingAgentTUI | null = null;

function setPiStatus(text: string): void {
  piStatus.textContent = text;
}

function updatePiFieldsDisabledState(provider: Provider): void {
  const isMock = provider === "mock";
  piEndpointInput.disabled = isMock;
  piApiKeyInput.disabled = isMock;
}

function writePiForm(config: PiConfig): void {
  piProviderSelect.value = config.provider;
  piModelInput.value = config.modelId;
  piEndpointInput.value = config.baseUrl;
  piApiKeyInput.value = config.apiKey;
  updatePiFieldsDisabledState(config.provider);
}

function readPiForm(): PiConfig {
  return normalizePiConfig({
    provider: piProviderSelect.value as Provider,
    modelId: piModelInput.value,
    baseUrl: piEndpointInput.value,
    apiKey: piApiKeyInput.value,
  });
}

function savePiConfig(config: PiConfig): void {
  window.localStorage.setItem(PI_STORAGE_KEY, JSON.stringify(config));
}

async function startPiTui(config: PiConfig): Promise<void> {
  currentPiConfig = config;
  savePiConfig(config);

  piApplyButton.disabled = true;
  setPiStatus(`Starting ${config.provider}/${config.modelId}…`);

  activePiApp?.stop();
  activePiApp = null;
  piTerminalElement.innerHTML = "";

  const terminal = new WebTerminal(piTerminalElement, {
    fontFamily: "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
    fontSize: 14,
    theme: {
      background: "#101214",
      foreground: "#e7ecef",
      cursor: "#f5c542",
      selectionBackground: "#355070",
    },
  });

  await terminal.ready;

  const model =
    config.provider === "mock"
      ? createBrowserMockModel()
      : createOpenAICompatibleModel({
          baseUrl: config.baseUrl,
          modelId: config.modelId,
          provider: config.provider,
        });

  const app = new BrowserCodingAgentTUI({
    terminal,
    model,
    getApiKey(provider) {
      if (config.provider === "mock") {
        return undefined;
      }
      return provider === config.provider ? config.apiKey : undefined;
    },
    welcomeMessage:
      config.provider === "mock"
        ? "Pi browser TUI ready with the mock model. This embed has no TIC-80 control tools."
        : `Pi browser TUI ready with ${config.provider}/${config.modelId}. This embed has no TIC-80 control tools.`,
  });

  app.start();
  activePiApp = app;
  setPiStatus(`Using ${config.provider}/${config.modelId}`);
  piApplyButton.disabled = false;
}

piProviderSelect.addEventListener("change", () => {
  const nextProvider = piProviderSelect.value as Provider;
  const previousDefaults = DEFAULTS[currentPiConfig.provider];
  const nextDefaults = DEFAULTS[nextProvider];

  if (!piModelInput.value.trim() || piModelInput.value === previousDefaults.modelId) {
    piModelInput.value = nextDefaults.modelId;
  }

  if (!piEndpointInput.value.trim() || piEndpointInput.value === previousDefaults.baseUrl) {
    piEndpointInput.value = nextDefaults.baseUrl;
  }

  updatePiFieldsDisabledState(nextProvider);
});

piApplyButton.addEventListener("click", () => {
  const nextConfig = readPiForm();
  if (nextConfig.provider !== "mock" && nextConfig.apiKey.length === 0) {
    setPiStatus(`API key required for ${nextConfig.provider}`);
    return;
  }
  void startPiTui(nextConfig);
});

writePiForm(currentPiConfig);
await refreshHostStatus();
writeHostLog(`Loaded combined browser host. Runtime base: ${runtimeBaseUrl.toString()}`);
await startPiTui(currentPiConfig);

window.addEventListener("beforeunload", () => {
  activePiApp?.stop();
  void coordinator.stop({ suppressLog: true });
});
