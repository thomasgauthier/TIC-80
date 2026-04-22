/// <reference lib="dom" />

import "./style.css";

import { Agent, type AgentTool } from "@mariozechner/pi-agent-core";
import {
  fauxAssistantMessage,
  fauxText,
  fauxToolCall,
  type Message,
  type Model,
  registerFauxProvider,
  type ToolResultMessage,
  Type,
} from "@mariozechner/pi-ai";
import { WebTerminal } from "@mariozechner/pi-tui/browser";

import { AgentSession } from "../../../../pi-mono/packages/coding-agent/src/core/agent-session.js";
import type { ModelRegistry } from "../../../../pi-mono/packages/coding-agent/src/core/model-registry.js";
import { SessionManager } from "../../../../pi-mono/packages/coding-agent/src/core/session-manager.js";
import { SettingsManager } from "../../../../pi-mono/packages/coding-agent/src/core/settings-manager.js";
import { InteractiveMode } from "../../../../pi-mono/packages/coding-agent/src/modes/interactive/interactive-mode.js";

import { createBrowserResourceLoader } from "./browser-resource-loader.js";
import { BROWSER_WORKSPACE_CWD, BrowserWorkspace, createDefaultFs } from "./browser-workspace.js";
import { installBundledTic80ctlSkill } from "./bundled-skill.js";
import { McpFs } from "./mcp-fs.js";
import { createTic80ctlCommand, type Tic80CtlRunner } from "./tic80ctl-commands.js";

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

type SupportedProvider = "mock" | "openai" | "custom-openai";
type FormProvider = SupportedProvider;

type BrowserConfig = {
  provider: FormProvider;
  modelId: string;
  baseUrl: string;
  apiKey: string;
};

type BrowserAuthStorage = {
  list(): string[];
  get(provider: string): undefined;
  getOAuthProviders(): Array<{ id: string; name: string; usesCallbackServer?: boolean }>;
  logout(provider: string): void;
  login(provider?: string): Promise<void>;
};

type BrowserModelRegistry = {
  authStorage: BrowserAuthStorage;
  getAvailable(): Model<string>[];
  getError(): string | undefined;
  refresh(): void;
  find(provider: string, modelId: string): Model<string> | undefined;
  hasConfiguredAuth(model: Model<string>): boolean;
  getApiKeyAndHeaders(
    model: Model<string>,
  ): Promise<{ ok: true; apiKey: string; headers?: Record<string, string> } | { ok: false; error: string }>;
  isUsingOAuth(model: Model<string>): boolean;
  getApiKeyForProvider(provider: string): Promise<string | undefined>;
  registerProvider(): void;
  unregisterProvider(): void;
};

class BrowserRuntimeHost {
  constructor(readonly session: AgentSession) {}

  async newSession(): Promise<{ cancelled: true }> {
    return { cancelled: true };
  }

  async fork(): Promise<{ cancelled: true; selectedText?: string }> {
    return { cancelled: true };
  }

  async switchSession(): Promise<{ cancelled: true }> {
    return { cancelled: true };
  }

  async importFromJsonl(): Promise<{ cancelled: true }> {
    return { cancelled: true };
  }

  async dispose(): Promise<void> {}
}

const fauxRegistration = registerFauxProvider({
  provider: "browser-mock",
  models: [
    {
      id: "interactive-mock",
      name: "Interactive Mock",
      reasoning: false,
      input: ["text", "image"],
      contextWindow: 128_000,
      maxTokens: 8_192,
    },
  ],
});

const mockAuthStorage: BrowserAuthStorage = {
  list() {
    return [];
  },
  get() {
    return undefined;
  },
  getOAuthProviders() {
    return [];
  },
  logout() {},
  async login() {
    throw new Error("OAuth login is unavailable in the browser example");
  },
};

const DEFAULT_READ_LIMIT = 2_000;
const PI_STORAGE_KEY = "tic80-pi-browser-tui-config";
const DEFAULT_OPENAI_MODEL = "gpt-4.1-mini";
const DEFAULT_OPENAI_BASE_URL = "https://api.openai.com/v1";
const DEFAULT_CUSTOM_MODEL = "qwen2.5-coder:latest";
const DEFAULT_CUSTOM_BASE_URL = "http://localhost:11434/v1";

function createBrowserTools(workspace: BrowserWorkspace): Record<string, AgentTool> {
  return {
    read: {
      name: "read",
      label: "Read",
      description: "Read a file from the browser workspace",
      parameters: Type.Object({
        path: Type.String(),
        offset: Type.Optional(Type.Number()),
        limit: Type.Optional(Type.Number()),
      }),
      async execute(_toolCallId, params) {
        const typedParams = params as { path: string; offset?: number; limit?: number };
        const text = await workspace.readFile(typedParams.path);
        const lines = text.split("\n");
        const startLine = typedParams.offset ? Math.max(typedParams.offset - 1, 0) : 0;
        if (startLine >= lines.length) {
          throw new Error(`Offset ${typedParams.offset} is beyond end of file (${lines.length} lines total)`);
        }
        const requestedLimit = typedParams.limit ?? DEFAULT_READ_LIMIT;
        const endLine = Math.min(startLine + requestedLimit, lines.length);
        const selectedText = lines.slice(startLine, endLine).join("\n") || "(empty file)";
        const truncated = endLine < lines.length;
        const notice = truncated ? `\n\n[Showing lines ${startLine + 1}-${endLine} of ${lines.length}]` : "";
        return {
          content: [{ type: "text", text: `${selectedText}${notice}` }],
          details: {
            path: workspace.resolvePath(typedParams.path),
            offset: startLine + 1,
            limit: endLine - startLine,
            truncated,
          },
        };
      },
    },
    bash: {
      name: "bash",
      label: "Bash",
      description: "Execute bash in the browser workspace",
      parameters: Type.Object({
        command: Type.String(),
        timeout: Type.Optional(Type.Number()),
      }),
      async execute(_toolCallId, params) {
        const typedParams = params as { command: string; timeout?: number };
        const result = await workspace.exec(typedParams.command, {
          timeoutMs: typedParams.timeout !== undefined ? typedParams.timeout * 1_000 : undefined,
        });
        const sections = [result.stdout, result.stderr].filter((value) => value.length > 0);
        const outputText = sections.join(sections.length > 1 ? "\n" : "") || "(no output)";
        if (result.exitCode !== 0) {
          throw new Error(`${outputText}\n\nCommand exited with code ${result.exitCode}`);
        }
        return {
          content: [{ type: "text", text: outputText }],
          details: {
            command: typedParams.command,
            cwd: BROWSER_WORKSPACE_CWD,
            exitCode: result.exitCode,
          },
        };
      },
    },
    write: {
      name: "write",
      label: "Write",
      description: "Write content to a file in the browser workspace",
      parameters: Type.Object({
        path: Type.String(),
        content: Type.String(),
      }),
      async execute(_toolCallId, params) {
        const typedParams = params as { path: string; content: string };
        const absolutePath = workspace.resolvePath(typedParams.path);
        const dir = absolutePath.split("/").slice(0, -1).join("/");
        if (dir && dir !== BROWSER_WORKSPACE_CWD) {
          await workspace.mkdir(dir, { recursive: true });
        }
        await workspace.writeFile(typedParams.path, typedParams.content);
        return {
          content: [
            {
              type: "text",
              text: `Successfully wrote ${typedParams.content.length} bytes to ${typedParams.path}`,
            },
          ],
          details: {
            path: absolutePath,
            size: typedParams.content.length,
          },
        };
      },
    },
    edit: {
      name: "edit",
      label: "Edit",
      description: "Edit a single file using exact text replacement.",
      parameters: Type.Object({
        path: Type.String(),
        edits: Type.Array(
          Type.Object({
            oldText: Type.String(),
            newText: Type.String(),
          }),
        ),
      }),
      async execute(_toolCallId, params) {
        const typedParams = params as { path: string; edits: Array<{ oldText: string; newText: string }> };
        const absolutePath = workspace.resolvePath(typedParams.path);

        if (!(await workspace.exists(absolutePath))) {
          throw new Error(`File not found: ${typedParams.path}`);
        }

        let content = await workspace.readFile(absolutePath);

        for (let i = 0; i < typedParams.edits.length; i++) {
          const edit = typedParams.edits[i];
          const oldText = edit.oldText.replace(/\r\n/g, "\n");
          content = content.replace(/\r\n/g, "\n");

          const matchCount = content.split(oldText).length - 1;
          if (matchCount === 0) {
            throw new Error(`Could not find exact text for edits[${i}] in ${typedParams.path}.`);
          }
          if (matchCount > 1) {
            throw new Error(
              `Found ${matchCount} occurrences of edits[${i}] in ${typedParams.path}. Each oldText must be unique.`,
            );
          }
          content = content.replace(oldText, edit.newText);
        }

        await workspace.writeFile(absolutePath, content);
        return {
          content: [
            {
              type: "text",
              text: `Successfully replaced ${typedParams.edits.length} block(s) in ${typedParams.path}.`,
            },
          ],
          details: { path: absolutePath },
        };
      },
    },
  };
}

function isTextBlock(block: { type: string }): block is { type: "text"; text: string } {
  return block.type === "text";
}

function getLastUserText(messages: Message[]): string {
  for (let index = messages.length - 1; index >= 0; index--) {
    const message = messages[index];
    if (message.role !== "user") {
      continue;
    }
    if (typeof message.content === "string") {
      return message.content;
    }
    return message.content
      .filter((block): block is { type: "text"; text: string } => block.type === "text")
      .map((block) => block.text)
      .join("\n");
  }
  return "";
}

function getLastToolResult(messages: Message[]): ToolResultMessage | undefined {
  for (let index = messages.length - 1; index >= 0; index--) {
    const message = messages[index];
    if (message.role === "toolResult") {
      return message;
    }
  }
  return undefined;
}

const fauxResponses = Array.from({ length: 2_000 }, () => (context: { messages: Message[] }) => {
  const lastToolResult = getLastToolResult(context.messages);
  if (lastToolResult) {
    const rendered = lastToolResult.content
      .filter(isTextBlock)
      .map((block) => block.text)
      .join("\n");
    return fauxAssistantMessage([
      fauxText("I used the requested tool."),
      fauxText(rendered || "The tool returned no text."),
    ]);
  }

  const lastUserText = getLastUserText(context.messages).toLowerCase();
  if (lastUserText.includes("bash") || lastUserText.includes("command")) {
    return fauxAssistantMessage([
      fauxText("I’ll run a bash command first."),
      fauxToolCall("bash", { command: `echo ${JSON.stringify(lastUserText || "hello")}` }),
    ]);
  }

  return fauxAssistantMessage([
    fauxText("I’ll inspect a file first."),
    fauxToolCall("read", { path: "/README.md" }),
  ]);
});
fauxRegistration.setResponses(fauxResponses);

function getMockModel(): Model<string> {
  const candidate = fauxRegistration.getModel("interactive-mock");
  if (!candidate) {
    throw new Error("Missing faux model");
  }
  return candidate;
}

function createOpenAICompatibleModel(config: BrowserConfig): Model<"openai-completions"> {
  const provider = config.provider === "openai" ? "openai" : "custom-openai";
  return {
    id: config.modelId,
    name: config.modelId,
    api: "openai-completions",
    provider,
    baseUrl: config.baseUrl,
    reasoning: false,
    input: ["text", "image"],
    cost: {
      input: 0,
      output: 0,
      cacheRead: 0,
      cacheWrite: 0,
    },
    contextWindow: 128_000,
    maxTokens: 8_192,
    compat: {
      supportsStore: false,
      supportsDeveloperRole: false,
      supportsReasoningEffort: false,
      supportsUsageInStreaming: false,
      maxTokensField: "max_tokens",
      supportsStrictMode: false,
    },
  };
}

function createModel(config: BrowserConfig): Model<string> {
  if (config.provider === "mock") {
    return getMockModel();
  }
  return createOpenAICompatibleModel(config);
}

function getDefaultValues(provider: FormProvider): Pick<BrowserConfig, "modelId" | "baseUrl"> {
  switch (provider) {
    case "openai":
      return { modelId: DEFAULT_OPENAI_MODEL, baseUrl: DEFAULT_OPENAI_BASE_URL };
    case "custom-openai":
      return { modelId: DEFAULT_CUSTOM_MODEL, baseUrl: DEFAULT_CUSTOM_BASE_URL };
    default:
      return { modelId: "interactive-mock", baseUrl: "browser://mock" };
  }
}

function normalizeConfig(config: BrowserConfig): BrowserConfig {
  const defaults = getDefaultValues(config.provider);
  const modelId = config.modelId.trim() || defaults.modelId;
  const baseUrl = (config.baseUrl.trim() || defaults.baseUrl).replace(/\/$/, "");
  return {
    provider: config.provider,
    modelId,
    baseUrl,
    apiKey: config.apiKey.trim(),
  };
}

function readStoredConfig(): BrowserConfig {
  try {
    const raw = window.localStorage.getItem(PI_STORAGE_KEY);
    if (!raw) {
      return normalizeConfig({
        provider: "mock",
        modelId: "interactive-mock",
        baseUrl: "browser://mock",
        apiKey: "",
      });
    }
    const parsed = JSON.parse(raw) as Partial<BrowserConfig>;
    if (
      typeof parsed.provider !== "string" ||
      typeof parsed.modelId !== "string" ||
      typeof parsed.baseUrl !== "string" ||
      typeof parsed.apiKey !== "string"
    ) {
      throw new Error("Invalid stored browser config");
    }
    return normalizeConfig({
      provider: parsed.provider as FormProvider,
      modelId: parsed.modelId,
      baseUrl: parsed.baseUrl,
      apiKey: parsed.apiKey,
    });
  } catch {
    return normalizeConfig({
      provider: "mock",
      modelId: "interactive-mock",
      baseUrl: "browser://mock",
      apiKey: "",
    });
  }
}

function saveConfig(config: BrowserConfig): void {
  window.localStorage.setItem(PI_STORAGE_KEY, JSON.stringify(config));
}

function describeModel(model: Model<string>): string {
  return `${model.provider}/${model.id}`;
}

function parseCommandArgs(raw: string): string[] {
  return (raw.match(/(?:[^\s"']+|"[^"]*"|'[^']*')+/g) || []).map((arg) =>
    arg.replace(/^"(.*)"$/, "$1").replace(/^'(.*)'$/, "$1"),
  );
}

const appBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const runtimeBaseUrl = new URL("tic80-runtime/", appBaseUrl);

function runtimeUrl(fileName: string): string {
  return new URL(fileName, runtimeBaseUrl).toString();
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

function setPiStatus(text: string): void {
  piStatus.textContent = text;
}

const targetUrl = runtimeUrl("index.html");
let tic80Controller: Awaited<ReturnType<typeof createTic80CtlBrowser>> | null = null;
const coordinator = createBrowserTargetCoordinator({
  controllerFactory: async () => {
    tic80Controller = await createTic80CtlBrowser({
      coreModulePath: runtimeUrl("tic80ctl-browser-core.js"),
    });
    return tic80Controller;
  },
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

let workspace: BrowserWorkspace;

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

async function startAndBindFs(kind: "iframe" | "popup"): Promise<unknown> {
  const result = await coordinator.start(kind);
  if (result && (result as { exitCode?: number }).exitCode !== 0) {
    writeHostLog(`Start failed (exit=${(result as { exitCode?: number }).exitCode}). Keeping in-memory filesystem.`);
    return result;
  }
  if (tic80Controller) {
    workspace.setFs(new McpFs(tic80Controller.callTool.bind(tic80Controller)));
    await installBundledTic80ctlSkill(workspace);
    writeHostLog(`Switched workspace to MCP-backed filesystem (TIC-80 ${kind}) and reinstalled bundled skills.`);
  }
  return result;
}

async function stopAndResetFs(): Promise<void> {
  await coordinator.stop();
  const fallback = createDefaultFs();
  workspace.setFs(fallback);
  await installBundledTic80ctlSkill(workspace);
  writeHostLog("Switched workspace back to in-memory fallback and reinstalled bundled skills.");
}

const tic80ctlRunner: Tic80CtlRunner = {
  runCommand(argv: string[]) {
    return coordinator.runCommand(argv);
  },
  status() {
    return coordinator.status();
  },
  async startAndBind(kind: "iframe" | "popup") {
    const result = await startAndBindFs(kind);
    await refreshHostStatus();
    return result;
  },
  async stopAndReset() {
    await stopAndResetFs();
    await refreshHostStatus();
  },
};

const tic80ctlCommand = createTic80ctlCommand(tic80ctlRunner);

startIframeButton.addEventListener("click", () => {
  void runHostAction("start iframe", () => startAndBindFs("iframe"));
});

startPopupButton.addEventListener("click", () => {
  void runHostAction("start popup", () => startAndBindFs("popup"));
});

statusButton.addEventListener("click", () => {
  void runHostAction("status", () => coordinator.status());
});

runButton.addEventListener("click", () => {
  void runHostAction("run", () => coordinator.runCommand(["run"]));
});

stopButton.addEventListener("click", () => {
  void runHostAction("stop", () => stopAndResetFs());
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

const initialConfig = readStoredConfig();
let currentConfig = initialConfig;
let currentModel = createModel(initialConfig);
let activeSession: AgentSession | null = null;

const browserModelRegistry: BrowserModelRegistry = {
  authStorage: mockAuthStorage,
  getAvailable() {
    return this.hasConfiguredAuth(currentModel) ? [currentModel] : [];
  },
  getError() {
    return undefined;
  },
  refresh() {},
  find(provider, modelId) {
    return provider === currentModel.provider && modelId === currentModel.id ? currentModel : undefined;
  },
  hasConfiguredAuth(model) {
    if (model.provider === "browser-mock") {
      return true;
    }
    if (model.provider !== currentModel.provider) {
      return false;
    }
    return currentConfig.apiKey.length > 0;
  },
  async getApiKeyAndHeaders(model) {
    if (model.provider === "browser-mock") {
      return { ok: true as const, apiKey: "browser-mock-key" };
    }
    if (model.provider !== currentModel.provider || currentConfig.apiKey.length === 0) {
      return { ok: false as const, error: `No API key configured for ${model.provider}` };
    }
    return { ok: true as const, apiKey: currentConfig.apiKey };
  },
  isUsingOAuth() {
    return false;
  },
  async getApiKeyForProvider(provider) {
    if (provider === currentModel.provider && currentConfig.apiKey.length > 0) {
      return currentConfig.apiKey;
    }
    return undefined;
  },
  registerProvider() {},
  unregisterProvider() {},
};

function writePiForm(config: BrowserConfig): void {
  piProviderSelect.value = config.provider;
  piModelInput.value = config.modelId;
  piEndpointInput.value = config.baseUrl;
  piApiKeyInput.value = config.apiKey;
  piEndpointInput.disabled = config.provider === "mock";
  piApiKeyInput.disabled = config.provider === "mock";
}

function readPiForm(): BrowserConfig {
  return normalizeConfig({
    provider: piProviderSelect.value as FormProvider,
    modelId: piModelInput.value,
    baseUrl: piEndpointInput.value,
    apiKey: piApiKeyInput.value,
  });
}

async function applyConfig(config: BrowserConfig): Promise<void> {
  if (config.provider !== "mock" && config.apiKey.length === 0) {
    setPiStatus(`API key required for ${config.provider}`);
    return;
  }

  const nextModel = createModel(config);
  currentConfig = config;
  currentModel = nextModel;
  writePiForm(config);
  saveConfig(config);

  if (!activeSession) {
    setPiStatus(`Configured ${describeModel(nextModel)}.`);
    return;
  }

  piApplyButton.disabled = true;
  setPiStatus(`Applying ${describeModel(nextModel)}...`);
  try {
    await activeSession.setModel(nextModel);
    setPiStatus(`Using ${describeModel(nextModel)}.`);
  } catch (error) {
    setPiStatus(error instanceof Error ? error.message : String(error));
  } finally {
    piApplyButton.disabled = false;
  }
}

const webTerminal = new WebTerminal(piTerminalElement, {
  fontFamily: "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
  fontSize: 14,
  theme: {
    background: "#101214",
    foreground: "#e7ecef",
    cursor: "#f5c542",
    selectionBackground: "#355070",
  },
});

writePiForm(initialConfig);

piProviderSelect.addEventListener("change", () => {
  const defaults = getDefaultValues(piProviderSelect.value as FormProvider);
  if (!piModelInput.value.trim() || piModelInput.value === getDefaultValues(currentConfig.provider).modelId) {
    piModelInput.value = defaults.modelId;
  }
  if (!piEndpointInput.value.trim() || piEndpointInput.value === getDefaultValues(currentConfig.provider).baseUrl) {
    piEndpointInput.value = defaults.baseUrl;
  }
  piEndpointInput.disabled = piProviderSelect.value === "mock";
  piApiKeyInput.disabled = piProviderSelect.value === "mock";
});

piApplyButton.addEventListener("click", () => {
  void applyConfig(readPiForm());
});

async function main(): Promise<void> {
  if (typeof process !== "undefined") {
    process.env.PI_OFFLINE = "1";
  }

  await refreshHostStatus();
  writeHostLog(`Loaded combined browser host. Runtime base: ${runtimeBaseUrl.toString()}`);

  setPiStatus("Initializing InteractiveMode...");
  await webTerminal.ready;

  workspace = await BrowserWorkspace.create(undefined, [tic80ctlCommand]);
  await installBundledTic80ctlSkill(workspace);
  (window as Window & { __piBrowserWorkspace?: BrowserWorkspace }).__piBrowserWorkspace = workspace;
  const browserTools = createBrowserTools(workspace);
  const resourceLoader = createBrowserResourceLoader();

  const settingsManager = SettingsManager.inMemory({
    quietStartup: false,
    compaction: { enabled: false },
    retry: { enabled: false },
    theme: "dark",
  });
  const sessionManager = SessionManager.inMemory(BROWSER_WORKSPACE_CWD);

  const agent = new Agent({
    initialState: {
      systemPrompt:
        "You are Pi running in a browser MVP. Prefer using tools when useful so the UI can render tool calls.",
      model: currentModel,
      thinkingLevel: "off",
      tools: [],
    },
    getApiKey(provider) {
      if (provider === currentModel.provider && currentConfig.apiKey.length > 0) {
        return currentConfig.apiKey;
      }
      return undefined;
    },
  });

  const session = new AgentSession({
    agent,
    sessionManager,
    settingsManager,
    cwd: BROWSER_WORKSPACE_CWD,
    resourceLoader,
    customTools: [],
    modelRegistry: browserModelRegistry as unknown as ModelRegistry,
    baseToolsOverride: browserTools,
    initialActiveToolNames: ["read", "bash", "write", "edit"],
  });
  activeSession = session;

  const runtime = new BrowserRuntimeHost(session);
  const interactiveMode = new InteractiveMode(runtime as never, {
    terminal: webTerminal,
    verbose: true,
  });

  setPiStatus(`InteractiveMode running with ${describeModel(currentModel)}.`);
  await interactiveMode.run();
}

main().catch((error) => {
  setPiStatus(`Error: ${error instanceof Error ? error.message : String(error)}`);
  console.error(error);
});

window.addEventListener("beforeunload", () => {
  void coordinator.stop({ suppressLog: true });
});
