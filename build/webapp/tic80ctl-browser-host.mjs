const DEFAULT_TARGET_URL = "./index.html";
const POPUP_TOKEN_PARAM = "tic80ctl_popup_token";
const POPUP_ORIGIN_PARAM = "tic80ctl_popup_origin";

function noop() {}

function inferOrigin(targetUrl, fallbackOrigin) {
    if (typeof targetUrl !== "string" || !targetUrl) {
        return fallbackOrigin || "*";
    }

    if (/^(about:blank|data:|javascript:)/i.test(targetUrl)) {
        return fallbackOrigin || "*";
    }

    try {
        return new URL(targetUrl, fallbackOrigin || "http://localhost").origin;
    } catch (_error) {
        return fallbackOrigin || "*";
    }
}

function createPopupToken(windowObject) {
    const cryptoObject = windowObject && windowObject.crypto;
    if (cryptoObject && typeof cryptoObject.getRandomValues === "function") {
        const bytes = new Uint8Array(16);
        cryptoObject.getRandomValues(bytes);
        return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
    }

    return [
        Date.now().toString(16),
        Math.random().toString(16).slice(2),
        Math.random().toString(16).slice(2),
    ].join("-");
}

function buildPopupTargetUrl(targetUrl, parentOrigin, token, fallbackBaseHref) {
    const baseHref = fallbackBaseHref
        || (typeof window !== "undefined" && window.location ? window.location.href : "http://localhost/");
    const url = new URL(targetUrl || DEFAULT_TARGET_URL, baseHref);
    url.searchParams.set(POPUP_TOKEN_PARAM, token);

    if (parentOrigin && parentOrigin !== "*") {
        url.searchParams.set(POPUP_ORIGIN_PARAM, parentOrigin);
    } else {
        url.searchParams.delete(POPUP_ORIGIN_PARAM);
    }

    return url.toString();
}

export function createMockTic80CtlBrowser() {
    const state = {
        bound: false,
        initialized: false,
        targetKind: null,
        owned: false,
        closed: false,
        origin: null,
        windowHandle: null,
        commandLog: [],
    };

    return {
        async bindTarget(windowHandle, options = {}) {
            state.bound = true;
            state.initialized = true;
            state.targetKind = options.kind || null;
            state.owned = !!options.owned;
            state.closed = false;
            state.origin = options.origin || null;
            state.windowHandle = windowHandle || null;
        },

        async run(argv = []) {
            const command = Array.isArray(argv) && argv.length > 0 ? String(argv[0]) : "";
            state.commandLog.push(Array.isArray(argv) ? argv.slice() : []);

            return {
                stdout: command ? `mock:${command}` : "mock",
                stderr: "",
                exitCode: 0,
                json: {
                    ok: true,
                    argv: Array.isArray(argv) ? argv.slice() : [],
                    commandLog: state.commandLog.slice(),
                },
            };
        },

        async status() {
            return {
                bound: state.bound,
                initialized: state.initialized,
                targetKind: state.targetKind,
                owned: state.owned,
                closed: state.closed || !!(state.windowHandle && state.windowHandle.closed),
                origin: state.origin,
            };
        },

        async stop() {
            state.bound = false;
            state.initialized = false;
            state.closed = true;
        },

        async dispose() {
            state.bound = false;
            state.initialized = false;
            state.windowHandle = null;
        },
    };
}

export async function resolveControllerFactory(options = {}) {
    const globalObject = options.globalObject || globalThis;
    const modulePaths = Array.isArray(options.modulePaths) && options.modulePaths.length > 0
        ? options.modulePaths
        : ["./tic80ctl-browser.js", "./tic80ctl-browser.mjs"];

    if (typeof globalObject.createTic80CtlBrowser === "function") {
        return globalObject.createTic80CtlBrowser;
    }

    if (
        globalObject.Tic80CtlBrowser &&
        typeof globalObject.Tic80CtlBrowser.createTic80CtlBrowser === "function"
    ) {
        return globalObject.Tic80CtlBrowser.createTic80CtlBrowser;
    }

    for (const modulePath of modulePaths) {
        try {
            const imported = await import(modulePath);
            if (imported && typeof imported.createTic80CtlBrowser === "function") {
                return imported.createTic80CtlBrowser;
            }
        } catch (_error) {
            // Try the next conventional browser artifact path.
        }
    }

    if (options.allowMock) {
        return async function createMockFactory() {
            return createMockTic80CtlBrowser();
        };
    }

    throw new Error(
        "Browser tic80ctl runtime not found. Expected createTic80CtlBrowser() on window."
    );
}

export function createBrowserTargetCoordinator(options = {}) {
    const controllerFactory = options.controllerFactory;
    const openIframeTarget = options.openIframeTarget || (async function unsupportedIframeTarget() {
        throw new Error("Iframe target opener is not configured.");
    });
    const openPopupTarget = options.openPopupTarget || (async function unsupportedPopupTarget() {
        throw new Error("Popup target opener is not configured.");
    });
    const removeIframeTarget = options.removeIframeTarget || noop;
    const closePopupTarget = options.closePopupTarget || noop;
    const log = options.log || noop;

    let controller = null;
    let target = null;

    async function ensureController() {
        if (controller) {
            return controller;
        }

        if (typeof controllerFactory !== "function") {
            throw new Error("Browser tic80ctl controllerFactory is required.");
        }

        controller = await controllerFactory();
        if (!controller || typeof controller.bindTarget !== "function" || typeof controller.run !== "function") {
            throw new Error("Browser tic80ctl controller is missing bindTarget()/run().");
        }
        return controller;
    }

    async function cleanupOwnedTarget(currentTarget) {
        if (!currentTarget || !currentTarget.owned) {
            return;
        }

        if (currentTarget.kind === "iframe") {
            removeIframeTarget(currentTarget);
            return;
        }

        if (currentTarget.kind === "popup") {
            closePopupTarget(currentTarget);
        }
    }

    async function bindOwnedTarget(kind) {
        const nextTarget =
            kind === "popup" ? await openPopupTarget() : await openIframeTarget();

        const nextController = await ensureController();
        await nextController.bindTarget(nextTarget.windowHandle, {
            kind,
            owned: !!nextTarget.owned,
            origin: nextTarget.origin,
            popupChannelToken: nextTarget.popupChannelToken,
        });

        target = {
            ...nextTarget,
            kind,
            owned: nextTarget.owned !== false,
        };

        log(`Bound ${kind} target at ${target.origin || "unknown origin"}.`);
        return target;
    }

    return {
        async start(kind) {
            if (kind !== "iframe" && kind !== "popup") {
                throw new Error(`Unsupported target kind: ${kind}`);
            }

            await this.stop({ suppressLog: true });
            await bindOwnedTarget(kind);
            const result = await controller.run(["start"]);
            log(`tic80ctl start (${kind}) exit=${result.exitCode}`);
            return result;
        },

        async runCommand(argv) {
            const nextController = await ensureController();
            if (!target) {
                throw new Error("No TIC-80 browser target is bound. Start a session first.");
            }

            const result = await nextController.run(argv);
            log(`tic80ctl ${Array.isArray(argv) ? argv.join(" ") : ""} exit=${result.exitCode}`);
            return result;
        },

        async status() {
            if (!controller) {
                return {
                    bound: false,
                    initialized: false,
                    targetKind: null,
                    owned: false,
                    closed: true,
                    origin: null,
                };
            }

            return controller.status ? controller.status() : {
                bound: !!target,
                initialized: !!target,
                targetKind: target ? target.kind : null,
                owned: !!(target && target.owned),
                closed: !target,
                origin: target ? target.origin : null,
            };
        },

        async stop(options = {}) {
            const suppressLog = !!options.suppressLog;

            if (controller && typeof controller.stop === "function") {
                await controller.stop();
            }

            if (controller && typeof controller.dispose === "function") {
                await controller.dispose();
            }

            await cleanupOwnedTarget(target);
            target = null;
            controller = null;

            if (!suppressLog) {
                log("tic80ctl stop completed.");
            }
        },
    };
}

function createIframeTargetHost(options) {
    const documentObject = options.documentObject;
    const iframeHost = options.iframeHost;
    const targetUrl = options.targetUrl || DEFAULT_TARGET_URL;
    const parentOrigin = options.parentOrigin || "*";

    return async function openIframeTarget() {
        if (!documentObject || !iframeHost) {
            throw new Error("Iframe host container is unavailable.");
        }

        iframeHost.textContent = "";

        const iframe = documentObject.createElement("iframe");
        iframe.className = "tic80ctl-demo-frame";
        iframe.src = targetUrl;
        iframe.allow = "fullscreen";
        iframeHost.appendChild(iframe);

        await new Promise((resolve, reject) => {
            const timeoutId = setTimeout(() => {
                reject(new Error("Timed out waiting for iframe target to load."));
            }, 15000);

            iframe.addEventListener(
                "load",
                () => {
                    clearTimeout(timeoutId);
                    resolve();
                },
                { once: true }
            );
        });

        return {
            kind: "iframe",
            owned: true,
            windowHandle: iframe.contentWindow,
            origin: inferOrigin(targetUrl, parentOrigin),
            iframe,
        };
    };
}

function createPopupTargetHost(options) {
    const windowObject = options.windowObject;
    const targetUrl = options.targetUrl || DEFAULT_TARGET_URL;
    const parentOrigin = options.parentOrigin || "*";

    return async function openPopupTarget() {
        if (!windowObject || typeof windowObject.open !== "function") {
            throw new Error("window.open() is unavailable.");
        }

        const popupChannelToken = createPopupToken(windowObject);
        const popupTargetUrl = buildPopupTargetUrl(
            targetUrl,
            parentOrigin,
            popupChannelToken,
            windowObject.location && windowObject.location.href
        );
        const popup = windowObject.open(popupTargetUrl, "tic80ctl-browser-popup", "popup,width=980,height=760");
        if (!popup) {
            throw new Error("Popup was blocked by the browser.");
        }

        await new Promise((resolve, reject) => {
            const start = Date.now();
            const timerId = windowObject.setInterval(() => {
                if (popup.closed) {
                    windowObject.clearInterval(timerId);
                    reject(new Error("Popup closed before TIC-80 loaded."));
                    return;
                }

                if (Date.now() - start > 15000) {
                    windowObject.clearInterval(timerId);
                    reject(new Error("Timed out waiting for popup target to load."));
                    return;
                }

                try {
                    const href = popup.location && popup.location.href;
                    const readyState = popup.document && popup.document.readyState;
                    const hasCanvas = !!(popup.document && popup.document.getElementById && popup.document.getElementById("canvas"));
                    if (href && href !== "about:blank" && readyState === "complete" && hasCanvas) {
                        windowObject.clearInterval(timerId);
                        resolve();
                    }
                } catch (_error) {
                    // Keep polling through transient same-origin access races while the popup is loading.
                }
            }, 100);
        });

        return {
            kind: "popup",
            owned: true,
            windowHandle: popup,
            origin: inferOrigin(targetUrl, parentOrigin),
            popupChannelToken,
            popup,
        };
    };
}

function createLogWriter(logElement) {
    return function writeLog(message) {
        if (!logElement) {
            return;
        }

        const line = `[${new Date().toISOString()}] ${message}`;
        logElement.value = logElement.value ? `${logElement.value}\n${line}` : line;
        logElement.scrollTop = logElement.scrollHeight;
    };
}

export async function bootTic80CtlBrowserDemo(options = {}) {
    const windowObject = options.windowObject || window;
    const documentObject = options.documentObject || document;
    const query = new URLSearchParams(windowObject.location.search);
    const allowMock = options.allowMock ?? query.get("mock") === "1";
    const targetUrl = options.targetUrl || query.get("target") || DEFAULT_TARGET_URL;

    const statusElement = documentObject.getElementById("status");
    const logElement = documentObject.getElementById("log");
    const iframeHost = documentObject.getElementById("iframe-host");
    const startIframeButton = documentObject.getElementById("start-iframe");
    const startPopupButton = documentObject.getElementById("start-popup");
    const statusButton = documentObject.getElementById("status-button");
    const runButton = documentObject.getElementById("run-button");
    const stopButton = documentObject.getElementById("stop-button");

    const log = createLogWriter(logElement);
    const controllerFactory = await resolveControllerFactory({
        globalObject: windowObject,
        allowMock,
    });

    const coordinator = createBrowserTargetCoordinator({
        controllerFactory,
        openIframeTarget: createIframeTargetHost({
            documentObject,
            iframeHost,
            targetUrl,
            parentOrigin: windowObject.location.origin,
        }),
        openPopupTarget: createPopupTargetHost({
            windowObject,
            targetUrl,
            parentOrigin: windowObject.location.origin,
        }),
        removeIframeTarget(currentTarget) {
            if (currentTarget && currentTarget.iframe && currentTarget.iframe.parentNode) {
                currentTarget.iframe.parentNode.removeChild(currentTarget.iframe);
            }
        },
        closePopupTarget(currentTarget) {
            if (currentTarget && currentTarget.popup && !currentTarget.popup.closed) {
                currentTarget.popup.close();
            }
        },
        log,
    });

    async function refreshStatus() {
        const currentStatus = await coordinator.status();
        if (statusElement) {
            statusElement.textContent = JSON.stringify(currentStatus, null, 2);
        }
        return currentStatus;
    }

    async function runWithUi(label, callback) {
        try {
            const result = await callback();
            log(`${label}: ${result && typeof result === "object" ? JSON.stringify(result) : String(result)}`);
        } catch (error) {
            log(`${label} failed: ${error && error.message ? error.message : String(error)}`);
        }
        await refreshStatus();
    }

    if (startIframeButton) {
        startIframeButton.addEventListener("click", () => runWithUi("start iframe", () => coordinator.start("iframe")));
    }

    if (startPopupButton) {
        startPopupButton.addEventListener("click", () => runWithUi("start popup", () => coordinator.start("popup")));
    }

    if (statusButton) {
        statusButton.addEventListener("click", () => runWithUi("status", () => coordinator.status()));
    }

    if (runButton) {
        runButton.addEventListener("click", () => runWithUi("run", () => coordinator.runCommand(["run"])));
    }

    if (stopButton) {
        stopButton.addEventListener("click", () => runWithUi("stop", () => coordinator.stop()));
    }

    await refreshStatus();
    log(allowMock ? "Loaded demo with mock tic80ctl runtime." : "Loaded demo with browser tic80ctl runtime.");
    return coordinator;
}
