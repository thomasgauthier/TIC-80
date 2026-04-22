import { homedir } from "./polyfills/os.js";
import { join } from "./polyfills/path.js";

export const isBunBinary = false;
export const isBunRuntime = false;

export type InstallMethod = "bun-binary" | "npm" | "pnpm" | "yarn" | "bun" | "unknown";

export function detectInstallMethod(): InstallMethod {
	return "unknown";
}

export function getUpdateInstruction(_packageName: string): string {
	return "Update via the browser app";
}

const browserPackageDir = "/home/browser/.pi/agent";

export function getPackageDir(): string {
	return browserPackageDir;
}

export function getThemesDir(): string {
	return join(browserPackageDir, "src", "modes", "interactive", "theme");
}

export function getExportTemplateDir(): string {
	return join(browserPackageDir, "src", "core", "export-html");
}

export function getPackageJsonPath(): string {
	return join(browserPackageDir, "package.json");
}

export function getReadmePath(): string {
	return join(browserPackageDir, "README.md");
}

export function getDocsPath(): string {
	return join(browserPackageDir, "docs");
}

export function getExamplesPath(): string {
	return join(browserPackageDir, "examples");
}

export function getChangelogPath(): string {
	return join(browserPackageDir, "CHANGELOG.md");
}

export function getInteractiveAssetsDir(): string {
	return join(browserPackageDir, "src", "modes", "interactive", "assets");
}

export function getBundledInteractiveAssetPath(name: string): string {
	return join(getInteractiveAssetsDir(), name);
}

const packageJson = {
	name: "@mariozechner/pi-coding-agent",
	version: "0.66.1",
	piConfig: {
		name: "pi",
		configDir: ".pi",
	},
};

export const APP_NAME: string = packageJson.piConfig.name;
export const CONFIG_DIR_NAME: string = packageJson.piConfig.configDir;
export const VERSION: string = packageJson.version;
export const ENV_AGENT_DIR = `${APP_NAME.toUpperCase()}_CODING_AGENT_DIR`;

const DEFAULT_SHARE_VIEWER_URL = "https://pi.dev/session/";

export function getShareViewerUrl(gistId: string): string {
	const baseUrl = processLike.env.PI_SHARE_VIEWER_URL || DEFAULT_SHARE_VIEWER_URL;
	return `${baseUrl}#${gistId}`;
}

export function getAgentDir(): string {
	return join(homedir(), CONFIG_DIR_NAME, "agent");
}

export function getCustomThemesDir(): string {
	return join(getAgentDir(), "themes");
}

export function getModelsPath(): string {
	return join(getAgentDir(), "models.json");
}

export function getAuthPath(): string {
	return join(getAgentDir(), "auth.json");
}

export function getSettingsPath(): string {
	return join(getAgentDir(), "settings.json");
}

export function getToolsDir(): string {
	return join(getAgentDir(), "tools");
}

export function getBinDir(): string {
	return join(getAgentDir(), "bin");
}

export function getPromptsDir(): string {
	return join(getAgentDir(), "prompts");
}

export function getSessionsDir(): string {
	return join(getAgentDir(), "sessions");
}

export function getDebugLogPath(): string {
	return join(getAgentDir(), `${APP_NAME}-debug.log`);
}

const globalObject = globalThis as Record<string, unknown>;
const processLike = (globalObject.process ?? { env: {} }) as { env?: Record<string, string | undefined> };

if (!processLike.env) {
	processLike.env = {};
}

globalObject.process = processLike;

if (!processLike.env.PI_OFFLINE) {
	processLike.env.PI_OFFLINE = "1";
}
