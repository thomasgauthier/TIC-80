const DEFAULT_TIMEOUT_MS = 10000;
const DEFAULT_CORE_MODULE_PATH = "./tic80ctl-browser-core.js";

function makeError(message, extra = {}) {
    const error = new Error(message);
    Object.assign(error, extra);
    return error;
}

function normalizeOrigin(origin) {
    return origin && origin !== "" ? origin : "*";
}

function normalizeResult(raw) {
    const result = raw && typeof raw === "object" ? raw : {};
    const stdout = typeof result.stdout === "string" ? result.stdout : "";
    const stderr = typeof result.stderr === "string" ? result.stderr : "";
    const exitCode = Number.isInteger(result.exit_code) ? result.exit_code : 1;

    let json;
    if (stdout.trim()) {
        try {
            json = JSON.parse(stdout);
        } catch (_error) {
            json = undefined;
        }
    }

    return {
        stdout,
        stderr,
        exitCode,
        json,
    };
}

function parseCli(argvInput) {
    const argv = Array.isArray(argvInput)
        ? argvInput.map((value) => String(value))
        : [];
    const positional = [];
    let jsonOutput = false;

    for (const arg of argv) {
        if (arg === "--json") {
            jsonOutput = true;
            continue;
        }

        positional.push(arg);
    }

    return {
        argv,
        positional,
        jsonOutput,
        command: positional[0] || "",
        args: positional.slice(1),
    };
}

function serializeJson(value) {
    return JSON.stringify(value, null, 2);
}

function makeCliResult(stdout = "", stderr = "", exitCode = 0, json = undefined) {
    return {
        stdout,
        stderr,
        exitCode,
        json,
    };
}

function makeCliError(message, jsonOutput, command = "") {
    const payload = {
        ok: false,
        command,
        error: message,
    };

    return makeCliResult(
        jsonOutput ? serializeJson(payload) : "",
        message,
        1,
        payload
    );
}

function collectToolText(result) {
    if (!result || !Array.isArray(result.content)) {
        return "";
    }

    return result.content
        .filter((entry) => entry && entry.type === "text" && typeof entry.text === "string")
        .map((entry) => entry.text)
        .join("\n");
}

function normalizeToolEnvelope(rawResponse) {
    const envelope = rawResponse && typeof rawResponse === "object" ? rawResponse : {};
    const result = envelope.result && typeof envelope.result === "object"
        ? envelope.result
        : envelope;

    return {
        envelope,
        result,
        text: collectToolText(result),
        isError: Boolean(result && result.isError),
    };
}

function formatToolCliResult(rawResponse, jsonOutput, payload) {
    const normalized = normalizeToolEnvelope(rawResponse);
    const text = normalized.text || serializeJson(normalized.result || {});
    const json = payload || normalized.result || {};

    return makeCliResult(
        jsonOutput ? serializeJson(json) : text,
        "",
        normalized.isError ? 1 : 0,
        json
    );
}

function formatStatusPayload(status) {
    const normalized = status && typeof status === "object" ? status : {};
    return {
        bound: Boolean(normalized.bound),
        initialized: Boolean(normalized.initialized),
        targetKind: normalized.targetKind || null,
        owned: Boolean(normalized.owned),
        closed: Boolean(normalized.closed),
        origin: normalized.origin || null,
    };
}

function formatStatusText(status) {
    const payload = formatStatusPayload(status);
    if (!payload.bound) {
        return "no TIC-80 browser session is bound";
    }

    return [
        `bound: ${payload.bound}`,
        `initialized: ${payload.initialized}`,
        `target: ${payload.targetKind || "unknown"}`,
        `owned: ${payload.owned}`,
        `closed: ${payload.closed}`,
        `origin: ${payload.origin || "*"}`,
    ].join("\n");
}

async function defaultReadTextFile(path) {
    const response = await fetch(path, {
        credentials: "same-origin",
        cache: "no-store",
    });

    if (!response.ok) {
        throw makeError(`Failed to fetch ${path}: ${response.status} ${response.statusText}`);
    }

    return await response.text();
}

async function loadCoreFactory(options = {}) {
    if (typeof options.coreFactory === "function") {
        return options.coreFactory;
    }

    const modulePath = options.coreModulePath || DEFAULT_CORE_MODULE_PATH;
    const imported = await import(modulePath);

    if (typeof imported.default === "function") {
        return imported.default;
    }

    throw makeError(`Browser tic80ctl core factory not found in ${modulePath}.`);
}

function createTransportController(options = {}) {
    const state = {
        targetWindow: null,
        targetKind: null,
        owned: false,
        origin: "*",
        initialized: false,
        closed: true,
        pending: new Map(),
        requestId: 1,
        timeoutMs: options.timeoutMs || DEFAULT_TIMEOUT_MS,
        removeOwnedTarget: null,
        lastLoadTarget: "",
    };

    function assertBoundTarget() {
        if (!state.targetWindow) {
            throw makeError("No TIC-80 browser target is bound.");
        }

        if (typeof state.targetWindow.closed === "boolean" && state.targetWindow.closed) {
            state.closed = true;
            throw makeError("The bound TIC-80 browser target is closed.");
        }
    }

    function onMessage(event) {
        if (event.source !== state.targetWindow) return;

        const value = event.data;
        if (!value || typeof value !== "object") return;
        if (value.jsonrpc !== "2.0") return;

        if (value.id !== undefined && state.pending.has(value.id)) {
            const pending = state.pending.get(value.id);
            state.pending.delete(value.id);
            clearTimeout(pending.timer);

            if (value.error) {
                pending.reject(makeError(value.error.message || "MCP request failed.", { response: value }));
            } else {
                pending.resolve(value);
            }
        }
    }

    window.addEventListener("message", onMessage);

    async function request(method, params, requestOptions = {}) {
        assertBoundTarget();

        const id = state.requestId++;
        const payload = {
            jsonrpc: "2.0",
            id,
            method,
        };

        if (params !== undefined) {
            payload.params = params;
        }

        const timeoutMs = requestOptions.timeoutMs || state.timeoutMs;

        return await new Promise((resolve, reject) => {
            const timer = window.setTimeout(() => {
                state.pending.delete(id);
                reject(makeError(`Timed out waiting for MCP response to ${method}.`, { method, id }));
            }, timeoutMs);

            state.pending.set(id, { resolve, reject, timer, method });
            state.targetWindow.postMessage(payload, state.origin);
        });
    }

    async function notify(method, params) {
        assertBoundTarget();
        const payload = {
            jsonrpc: "2.0",
            method,
        };

        if (params !== undefined) {
            payload.params = params;
        }

        state.targetWindow.postMessage(payload, state.origin);
    }

    async function initialize() {
        if (state.initialized) return;

        await request("initialize", {
            protocolVersion: "2025-03-26",
            clientInfo: {
                name: "tic80ctl-browser",
                version: "0.1.0",
            },
            capabilities: {},
        });

        await notify("notifications/initialized", {});
        state.initialized = true;
        state.closed = false;
    }

    async function bindTarget(handle, bindOptions = {}) {
        if (!handle || typeof handle.postMessage !== "function") {
            throw makeError("bindTarget requires a Window-compatible handle.");
        }

        state.targetWindow = handle;
        state.targetKind = bindOptions.kind || "iframe";
        state.owned = Boolean(bindOptions.owned);
        state.origin = normalizeOrigin(bindOptions.origin);
        state.initialized = false;
        state.closed = false;
        state.removeOwnedTarget = typeof bindOptions.removeOwnedTarget === "function"
            ? bindOptions.removeOwnedTarget
            : null;
        state.lastLoadTarget = "";
    }

    async function bindIframe(iframe, bindOptions = {}) {
        if (!iframe || !iframe.contentWindow) {
            throw makeError("bindIframe requires a live iframe element.");
        }

        await bindTarget(iframe.contentWindow, {
            kind: "iframe",
            owned: bindOptions.owned !== undefined ? bindOptions.owned : true,
            origin: bindOptions.origin || "*",
            removeOwnedTarget: bindOptions.removeOwnedTarget || (() => iframe.remove()),
        });
    }

    async function openPopupTarget(url, popupOptions = {}) {
        const features = popupOptions.features || "popup,width=960,height=720";
        const popup = window.open(url, popupOptions.name || "tic80ctl-popup", features);
        if (!popup) {
            throw makeError("window.open returned null. The popup may have been blocked.");
        }

        await bindTarget(popup, {
            kind: "popup",
            owned: true,
            origin: popupOptions.origin || "*",
        });

        return popup;
    }

    async function callTool(name, argumentsObject = {}) {
        await initialize();
        return await request("tools/call", {
            name,
            arguments: argumentsObject,
        });
    }

    async function status() {
        const closed = !state.targetWindow || Boolean(state.targetWindow.closed);
        state.closed = closed;

        return {
            bound: Boolean(state.targetWindow),
            initialized: state.initialized,
            targetKind: state.targetKind,
            owned: state.owned,
            closed,
            origin: state.origin,
        };
    }

    async function stop() {
        for (const [id, pending] of state.pending) {
            clearTimeout(pending.timer);
            pending.reject(makeError("Session stopped before response completed.", { id }));
        }
        state.pending.clear();

        if (state.owned && state.targetWindow) {
            if (state.targetKind === "popup" && typeof state.targetWindow.close === "function") {
                state.targetWindow.close();
            } else if (state.targetKind === "iframe" && state.removeOwnedTarget) {
                state.removeOwnedTarget();
            }
        }

        state.targetWindow = null;
        state.targetKind = null;
        state.owned = false;
        state.origin = "*";
        state.initialized = false;
        state.closed = true;
        state.removeOwnedTarget = null;
        state.lastLoadTarget = "";
    }

    async function dispose() {
        await stop();
        window.removeEventListener("message", onMessage);
    }

    function getState() {
        return {
            targetKind: state.targetKind,
            owned: state.owned,
            origin: state.origin,
            initialized: state.initialized,
            running: Boolean(state.targetWindow) && !state.closed,
            closed: state.closed,
        };
    }

    function setLastLoadTarget(path) {
        state.lastLoadTarget = path || "";
    }

    function getLastLoadTarget() {
        return state.lastLoadTarget || "";
    }

    return {
        bindTarget,
        bindIframe,
        openPopupTarget,
        initialize,
        request,
        notify,
        callTool,
        status,
        stop,
        dispose,
        getState,
        setLastLoadTarget,
        getLastLoadTarget,
    };
}

function createHostBridge(transport, options = {}) {
    const readTextFile = typeof options.readTextFile === "function"
        ? options.readTextFile
        : defaultReadTextFile;

    return {
        async invoke(requestJson) {
            const request = JSON.parse(requestJson || "{}");

            switch (request.op) {
            case "start": {
                await transport.initialize();
                const status = await transport.status();
                return {
                    ok: true,
                    running: status.bound && !status.closed,
                    initialized: status.initialized,
                    owned: status.owned,
                    closed: status.closed,
                    target_kind: status.targetKind || "",
                    origin: status.origin || "",
                };
            }

            case "status": {
                const status = await transport.status();
                return {
                    ok: true,
                    running: status.bound && !status.closed,
                    initialized: status.initialized,
                    owned: status.owned,
                    closed: status.closed,
                    target_kind: status.targetKind || "",
                    origin: status.origin || "",
                };
            }

            case "stop": {
                const before = await transport.status();
                await transport.stop();
                return {
                    ok: true,
                    stopped: true,
                    closed: before.owned || before.closed,
                };
            }

            case "read_text_file": {
                const text = await readTextFile(request.path);
                return {
                    ok: true,
                    text,
                };
            }

            case "tool": {
                if (!request.tool || typeof request.tool !== "string") {
                    return { ok: false, error: "tool op requires a string tool name" };
                }

                if (request.tool === "run_command" && request.arguments && typeof request.arguments.command === "string") {
                    const command = request.arguments.command.trim();
                    if (command.startsWith("load ")) {
                        transport.setLastLoadTarget(command.slice(5).trim());
                    }
                }

                const rawResponse = await transport.callTool(request.tool, request.arguments || {});
                const result = rawResponse && typeof rawResponse.result === "object"
                    ? rawResponse.result
                    : rawResponse;

                return {
                    ok: true,
                    is_error: Boolean(result && result.isError),
                    mcp_raw: JSON.stringify(result || {}),
                    verb: request.request_text || request.tool,
                    last_load_target: transport.getLastLoadTarget(),
                    stderr_tail: "",
                };
            }

            default:
                return {
                    ok: false,
                    error: `Unsupported browser tic80ctl host op: ${request.op || "<empty>"}`,
                };
            }
        },
    };
}

async function runTransportBuiltin(transport, argvInput, options = {}) {
    const readTextFile = typeof options.readTextFile === "function"
        ? options.readTextFile
        : defaultReadTextFile;
    const parsed = parseCli(argvInput);

    if (!parsed.command) {
        return makeCliError("tic80ctl: command is required", parsed.jsonOutput);
    }

    switch (parsed.command) {
    case "start": {
        if (parsed.args.length !== 0) {
            return makeCliError(
                "tic80ctl: start does not accept additional arguments",
                parsed.jsonOutput,
                parsed.command
            );
        }

        await transport.initialize();
        const status = formatStatusPayload(await transport.status());
        const payload = {
            ok: true,
            ...status,
        };
        const stdout = parsed.jsonOutput
            ? serializeJson(payload)
            : `started browser tic80ctl session (${status.targetKind || "target"} @ ${status.origin || "*"})`;
        return makeCliResult(stdout, "", 0, payload);
    }

    case "status": {
        if (parsed.args.length !== 0) {
            return makeCliError(
                "tic80ctl: status does not accept additional arguments",
                parsed.jsonOutput,
                parsed.command
            );
        }

        const status = formatStatusPayload(await transport.status());
        return makeCliResult(
            parsed.jsonOutput ? serializeJson(status) : formatStatusText(status),
            "",
            0,
            status
        );
    }

    case "stop": {
        if (parsed.args.length !== 0) {
            return makeCliError(
                "tic80ctl: stop does not accept additional arguments",
                parsed.jsonOutput,
                parsed.command
            );
        }

        const before = formatStatusPayload(await transport.status());
        await transport.stop();
        const payload = {
            ok: true,
            stopped: true,
            bound: false,
            initialized: false,
            targetKind: before.targetKind,
            owned: before.owned,
            closed: before.owned || before.closed,
            origin: before.origin,
        };
        return makeCliResult(
            parsed.jsonOutput ? serializeJson(payload) : "stopped browser tic80ctl session",
            "",
            0,
            payload
        );
    }

    case "cmd": {
        if (parsed.args.length === 0) {
            return makeCliError("tic80ctl: cmd requires a TIC-80 command string", parsed.jsonOutput, parsed.command);
        }

        const commandText = parsed.args[0];
        const rawResponse = await transport.callTool("run_command", { command: commandText });
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "cmd",
            request: {
                tool: "run_command",
                arguments: { command: commandText },
            },
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    case "load": {
        if (parsed.args.length === 0) {
            return makeCliError("tic80ctl: load requires a cart path", parsed.jsonOutput, parsed.command);
        }

        const loadTarget = parsed.args[0];
        transport.setLastLoadTarget(loadTarget);
        const rawResponse = await transport.callTool("run_command", { command: `load ${loadTarget}` });
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "load",
            target: loadTarget,
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    case "run": {
        if (parsed.args.length !== 0) {
            return makeCliError("tic80ctl: run does not accept additional arguments", parsed.jsonOutput, parsed.command);
        }

        const rawResponse = await transport.callTool("run_command", { command: "run" });
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "run",
            lastLoadTarget: transport.getLastLoadTarget(),
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    case "eval": {
        if (parsed.args.length === 0) {
            return makeCliError("tic80ctl: eval requires an expression", parsed.jsonOutput, parsed.command);
        }

        const expression = parsed.args[0];
        const rawResponse = await transport.callTool("run_command", { command: `eval ${expression}` });
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "eval",
            expression,
            lastLoadTarget: transport.getLastLoadTarget(),
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    case "screenshot": {
        if (parsed.args.length > 1) {
            return makeCliError(
                "tic80ctl: screenshot accepts at most one path argument",
                parsed.jsonOutput,
                parsed.command
            );
        }

        const path = parsed.args[0];
        const rawResponse = await transport.callTool(
            "capture_screenshot",
            path ? { path } : {}
        );
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "screenshot",
            path: path || "",
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    case "playtest": {
        let scriptFile = "";
        let inputOverlay = true;
        let timeoutSeconds;

        for (let index = 0; index < parsed.args.length; index++) {
            const arg = parsed.args[index];
            if (arg === "--script-file") {
                scriptFile = parsed.args[index + 1] || "";
                index++;
                continue;
            }

            if (arg === "--timeout") {
                const rawValue = parsed.args[index + 1];
                index++;
                if (rawValue === undefined) {
                    return makeCliError("tic80ctl: --timeout requires a value", parsed.jsonOutput, parsed.command);
                }

                timeoutSeconds = Number.parseInt(rawValue, 10);
                if (!Number.isFinite(timeoutSeconds)) {
                    return makeCliError("tic80ctl: --timeout must be an integer", parsed.jsonOutput, parsed.command);
                }
                continue;
            }

            if (arg === "--input-overlay") {
                inputOverlay = true;
                continue;
            }

            if (arg === "--no-input-overlay") {
                inputOverlay = false;
                continue;
            }

            return makeCliError(`tic80ctl: unknown playtest option: ${arg}`, parsed.jsonOutput, parsed.command);
        }

        if (!scriptFile) {
            return makeCliError("tic80ctl: --script-file is required", parsed.jsonOutput, parsed.command);
        }

        const script = await readTextFile(scriptFile);
        const argumentsObject = {
            script,
            input_overlay: inputOverlay,
        };

        if (timeoutSeconds !== undefined) {
            argumentsObject.timeout_seconds = timeoutSeconds;
        }

        const rawResponse = await transport.callTool("run_playtest_episode", argumentsObject);
        return formatToolCliResult(rawResponse, parsed.jsonOutput, {
            command: "playtest",
            scriptFile,
            response: normalizeToolEnvelope(rawResponse).result,
        });
    }

    default:
        return null;
    }
}

function createCoreRunner(module, transport, options = {}) {
    module.tic80ctlBrowserHost = createHostBridge(transport, options);

    return async function run(argv) {
        const raw = await module.ccall(
            "tic80ctl_browser_run_from_json",
            "string",
            ["string"],
            [JSON.stringify({ argv: Array.isArray(argv) ? argv : [] })],
            { async: true }
        );

        return normalizeResult(raw ? JSON.parse(raw) : {});
    };
}

export async function createTic80CtlBrowser(options = {}) {
    const transport = createTransportController(options);
    let coreState = null;

    async function getCoreState() {
        if (coreState) {
            return coreState;
        }

        const createCore = await loadCoreFactory(options);
        const module = await createCore();
        coreState = {
            module,
            runCore: createCoreRunner(module, transport, options),
        };
        return coreState;
    }

    return {
        bindTarget: transport.bindTarget,
        bindIframe: transport.bindIframe,
        openPopupTarget: transport.openPopupTarget,
        initialize: transport.initialize,
        request: transport.request,
        callTool: async (name, argumentsObject = {}) => {
            const rawResponse = await transport.callTool(name, argumentsObject);
            return rawResponse && typeof rawResponse.result === "object"
                ? rawResponse.result
                : rawResponse;
        },
        run: async (argv) => {
            const builtin = await runTransportBuiltin(transport, argv, options);
            if (builtin) {
                return builtin;
            }

            const { runCore } = await getCoreState();
            return await runCore(argv);
        },
        status: transport.status,
        stop: transport.stop,
        dispose: async () => {
            if (coreState && coreState.module) {
                delete coreState.module.tic80ctlBrowserHost;
            }
            await transport.dispose();
        },
    };
}
