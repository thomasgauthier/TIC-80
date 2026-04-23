import { createSyntheticSourceInfo } from "../../../../pi-mono/packages/coding-agent/src/core/source-info.js";

import type { BrowserWorkspace } from "./browser-workspace.js";
import { registerFileContent } from "./polyfills/fs.js";

import extensionSource from "./bundled-extensions/tic80ctl-lint-cart-on-lua-write.ts?raw";

const EXTENSION_BASE_DIR = "/workspace/.pi/extensions";
export const BUNDLED_TIC80_LINT_EXTENSION_PATH = `${EXTENSION_BASE_DIR}/tic80ctl-lint-cart-on-lua-write.ts`;

export const bundledTic80LintExtensionSourceInfo = createSyntheticSourceInfo(BUNDLED_TIC80_LINT_EXTENSION_PATH, {
	source: "local",
	scope: "project",
	baseDir: EXTENSION_BASE_DIR,
});

export const bundledTic80LintExtensionFile = {
	path: BUNDLED_TIC80_LINT_EXTENSION_PATH,
	content: extensionSource,
} as const;

function getParentDir(filePath: string): string {
	const slashIndex = filePath.lastIndexOf("/");
	return slashIndex <= 0 ? "/" : filePath.slice(0, slashIndex);
}

export async function installBundledTic80LintExtension(workspace: BrowserWorkspace): Promise<void> {
	const dir = getParentDir(bundledTic80LintExtensionFile.path);
	await workspace.mkdir(dir, { recursive: true });
	await workspace.writeFile(bundledTic80LintExtensionFile.path, bundledTic80LintExtensionFile.content);
	registerFileContent(bundledTic80LintExtensionFile.path, bundledTic80LintExtensionFile.content);
}
