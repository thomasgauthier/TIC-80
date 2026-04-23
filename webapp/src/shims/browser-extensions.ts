import { createEventBus } from "../../../../../pi-mono/packages/coding-agent/src/core/event-bus.js";
import { createSyntheticSourceInfo } from "../../../../../pi-mono/packages/coding-agent/src/core/source-info.js";
import {
	defineTool,
	isBashToolResult,
	isEditToolResult,
	isFindToolResult,
	isGrepToolResult,
	isLsToolResult,
	isReadToolResult,
	isToolCallEventType,
	isWriteToolResult,
} from "../../../../../pi-mono/packages/coding-agent/src/core/extensions/types.js";
import { ExtensionRunner as PiExtensionRunner } from "../../../../../pi-mono/packages/coding-agent/src/core/extensions/runner.js";
import { wrapRegisteredTool, wrapRegisteredTools } from "../../../../../pi-mono/packages/coding-agent/src/core/extensions/wrapper.js";

function getBrowserWorkspace() {
	return (window as Window & { __piBrowserWorkspace?: any }).__piBrowserWorkspace;
}

function shellQuote(value: string): string {
	return `'${value.replace(/'/g, `'"'"'`)}'`;
}

async function execInBrowserWorkspace(
	command: string,
	args: string[],
	_cwd: string,
	options?: { signal?: AbortSignal; timeout?: number },
): Promise<{ stdout: string; stderr: string; code: number; killed: boolean }> {
	const workspace = getBrowserWorkspace();
	if (!workspace) {
		return {
			stdout: "",
			stderr: "Browser workspace not initialized",
			code: 1,
			killed: false,
		};
	}

	let script: string;
	if (command === "bash" && args[0] === "-lc" && typeof args[1] === "string") {
		script = args[1];
	} else {
		script = [command, ...args.map((arg) => shellQuote(arg))].join(" ");
	}

	try {
		const result = await workspace.exec(script, {
			timeoutMs: options?.timeout,
			signal: options?.signal,
		});
		return {
			stdout: result.stdout,
			stderr: result.stderr,
			code: result.exitCode,
			killed: false,
		};
	} catch (error) {
		return {
			stdout: "",
			stderr: error instanceof Error ? error.message : String(error),
			code: 1,
			killed: !!options?.signal?.aborted,
		};
	}
}

function createExtensionRuntime() {
	const notInitialized = () => {
		throw new Error("Extension runtime not initialized. Action methods cannot be called during extension loading.");
	};

	const runtime = {
		sendMessage: notInitialized,
		sendUserMessage: notInitialized,
		appendEntry: notInitialized,
		setSessionName: notInitialized,
		getSessionName: notInitialized,
		setLabel: notInitialized,
		getActiveTools: notInitialized,
		getAllTools: notInitialized,
		setActiveTools: notInitialized,
		refreshTools: () => {},
		getCommands: notInitialized,
		setModel: () => Promise.reject(new Error("Extension runtime not initialized")),
		getThinkingLevel: notInitialized,
		setThinkingLevel: notInitialized,
		flagValues: new Map<string, boolean | string>(),
		pendingProviderRegistrations: [] as Array<{ name: string; config: unknown; extensionPath: string }>,
		registerProvider: (name: string, config: unknown, extensionPath = "<unknown>") => {
			runtime.pendingProviderRegistrations.push({ name, config, extensionPath });
		},
		unregisterProvider: (name: string) => {
			runtime.pendingProviderRegistrations = runtime.pendingProviderRegistrations.filter((r) => r.name !== name);
		},
	};

	return runtime;
}

function createExtension(extensionPath: string, resolvedPath: string) {
	const baseDir = extensionPath.startsWith("<") ? undefined : resolvedPath.slice(0, resolvedPath.lastIndexOf("/"));
	return {
		path: extensionPath,
		resolvedPath,
		sourceInfo: createSyntheticSourceInfo(extensionPath, { source: "local", baseDir }),
		handlers: new Map(),
		tools: new Map(),
		messageRenderers: new Map(),
		commands: new Map(),
		flags: new Map(),
		shortcuts: new Map(),
	};
}

function createExtensionAPI(extension: any, runtime: any, cwd: string, eventBus: any) {
	return {
		on(event: string, handler: (...args: unknown[]) => Promise<unknown>): void {
			const list = extension.handlers.get(event) ?? [];
			list.push(handler);
			extension.handlers.set(event, list);
		},
		registerTool(tool: any): void {
			extension.tools.set(tool.name, {
				definition: tool,
				sourceInfo: extension.sourceInfo,
			});
			runtime.refreshTools();
		},
		registerCommand(name: string, options: any): void {
			extension.commands.set(name, {
				name,
				sourceInfo: extension.sourceInfo,
				...options,
			});
		},
		registerShortcut(shortcut: string, options: any): void {
			extension.shortcuts.set(shortcut, { shortcut, extensionPath: extension.path, ...options });
		},
		registerFlag(name: string, options: any): void {
			extension.flags.set(name, { name, extensionPath: extension.path, ...options });
			if (options.default !== undefined && !runtime.flagValues.has(name)) {
				runtime.flagValues.set(name, options.default);
			}
		},
		registerMessageRenderer(customType: string, renderer: any): void {
			extension.messageRenderers.set(customType, renderer);
		},
		getFlag(name: string) {
			if (!extension.flags.has(name)) return undefined;
			return runtime.flagValues.get(name);
		},
		sendMessage(message: any, options: any): void {
			runtime.sendMessage(message, options);
		},
		sendUserMessage(content: any, options: any): void {
			runtime.sendUserMessage(content, options);
		},
		appendEntry(customType: string, data?: unknown): void {
			runtime.appendEntry(customType, data);
		},
		setSessionName(name: string): void {
			runtime.setSessionName(name);
		},
		getSessionName(): string | undefined {
			return runtime.getSessionName();
		},
		setLabel(entryId: string, label: string | undefined): void {
			runtime.setLabel(entryId, label);
		},
		exec(command: string, args: string[], options?: { signal?: AbortSignal; timeout?: number; cwd?: string }) {
			return execInBrowserWorkspace(command, args, options?.cwd ?? cwd, options);
		},
		getActiveTools(): string[] {
			return runtime.getActiveTools();
		},
		getAllTools() {
			return runtime.getAllTools();
		},
		setActiveTools(toolNames: string[]): void {
			runtime.setActiveTools(toolNames);
		},
		getCommands() {
			return runtime.getCommands();
		},
		setModel(model: unknown) {
			return runtime.setModel(model);
		},
		getThinkingLevel() {
			return runtime.getThinkingLevel();
		},
		setThinkingLevel(level: unknown) {
			runtime.setThinkingLevel(level);
		},
		registerProvider(name: string, config: unknown) {
			runtime.registerProvider(name, config, extension.path);
		},
		unregisterProvider(name: string) {
			runtime.unregisterProvider(name, extension.path);
		},
			events: eventBus,
	};
}

async function loadExtensionFromFactory(factory: any, extensionPath: string, cwd: string, runtimeArg?: any, eventBusArg?: any) {
	const runtimeToUse = runtimeArg ?? createExtensionRuntime();
	const eventBus = eventBusArg ?? createEventBus();
	const extension = createExtension(extensionPath, extensionPath);
	const api = createExtensionAPI(extension, runtimeToUse, cwd, eventBus);
	await factory(api);
	return extension;
}

async function discoverAndLoadExtensions() {
	return { extensions: [], errors: [], runtime: createExtensionRuntime() };
}

async function loadExtensions() {
	return { extensions: [], errors: [], runtime: createExtensionRuntime() };
}

class ExtensionRunner extends PiExtensionRunner {
	createContext() {
		const ctx = super.createContext() as any;
		const workspace = getBrowserWorkspace();
		if (workspace) {
			ctx.fs = workspace.fs;
		}
		return ctx;
	}

	createCommandContext() {
		const ctx = super.createCommandContext() as any;
		const workspace = getBrowserWorkspace();
		if (workspace) {
			ctx.fs = workspace.fs;
		}
		return ctx;
	}
}

export {
	ExtensionRunner,
	createExtensionRuntime,
	defineTool,
	discoverAndLoadExtensions,
	isBashToolResult,
	isEditToolResult,
	isFindToolResult,
	isGrepToolResult,
	isLsToolResult,
	isReadToolResult,
	isToolCallEventType,
	isWriteToolResult,
	loadExtensionFromFactory,
	loadExtensions,
	wrapRegisteredTool,
	wrapRegisteredTools,
};
