import { createSyntheticSourceInfo } from "../../../../pi-mono/packages/coding-agent/src/core/source-info.js";

import type { BrowserWorkspace } from "./browser-workspace.js";
import { registerFileContent } from "./polyfills/fs.js";

import seleneExtensionSource from "./bundled-extensions/selene-on-lua-write.ts?raw";
import tic80LintExtensionSource from "./bundled-extensions/tic80ctl-lint-cart-on-lua-write.ts?raw";

const EXTENSION_BASE_DIR = "/workspace/.pi/extensions";

export const BUNDLED_TIC80_LINT_EXTENSION_PATH = `${EXTENSION_BASE_DIR}/tic80ctl-lint-cart-on-lua-write.ts`;
export const BUNDLED_SELENE_EXTENSION_PATH = `${EXTENSION_BASE_DIR}/selene-on-lua-write.ts`;

export type BundledExtensionFile = {
	path: string;
	content: string;
	sourceInfo: ReturnType<typeof createSyntheticSourceInfo>;
};

export const bundledExtensionFiles: BundledExtensionFile[] = [
	{
		path: BUNDLED_TIC80_LINT_EXTENSION_PATH,
		content: tic80LintExtensionSource,
		sourceInfo: createSyntheticSourceInfo(BUNDLED_TIC80_LINT_EXTENSION_PATH, {
			source: "local",
			scope: "project",
			baseDir: EXTENSION_BASE_DIR,
		}),
	},
	{
		path: BUNDLED_SELENE_EXTENSION_PATH,
		content: seleneExtensionSource,
		sourceInfo: createSyntheticSourceInfo(BUNDLED_SELENE_EXTENSION_PATH, {
			source: "local",
			scope: "project",
			baseDir: EXTENSION_BASE_DIR,
		}),
	},
];

function getParentDir(filePath: string): string {
	const slashIndex = filePath.lastIndexOf("/");
	return slashIndex <= 0 ? "/" : filePath.slice(0, slashIndex);
}

export async function installBundledExtensions(workspace: BrowserWorkspace): Promise<void> {
	for (const file of bundledExtensionFiles) {
		const dir = getParentDir(file.path);
		await workspace.mkdir(dir, { recursive: true });
		await workspace.writeFile(file.path, file.content);
		registerFileContent(file.path, file.content);
	}
}
