import { createEventBus } from "../../../../pi-mono/packages/coding-agent/src/core/event-bus.js";
import type { ResourceLoader } from "../../../../pi-mono/packages/coding-agent/src/core/resource-loader.js";

import { bundledTic80LintExtensionFactory } from "./bundled-extension-runtime.js";
import { bundledTic80LintExtensionSourceInfo, BUNDLED_TIC80_LINT_EXTENSION_PATH } from "./bundled-extension.js";
import { bundledTic80ctlUsageSkill } from "./bundled-skill.js";
import { createExtensionRuntime, loadExtensionFromFactory } from "./shims/browser-extensions.js";

class BrowserResourceLoader implements ResourceLoader {
	private extensionsResult = {
		extensions: [],
		errors: [] as Array<{ path: string; error: string }>,
		runtime: createExtensionRuntime(),
	};

	async reload(): Promise<void> {
		const runtime = createExtensionRuntime();
		const eventBus = createEventBus();
		try {
			const extension = await loadExtensionFromFactory(
				bundledTic80LintExtensionFactory,
				BUNDLED_TIC80_LINT_EXTENSION_PATH,
				"/workspace",
				runtime,
				eventBus,
			);
			extension.sourceInfo = bundledTic80LintExtensionSourceInfo;
			this.extensionsResult = {
				extensions: [extension],
				errors: [],
				runtime,
			};
		} catch (error) {
			this.extensionsResult = {
				extensions: [],
				errors: [
					{
						path: BUNDLED_TIC80_LINT_EXTENSION_PATH,
						error: error instanceof Error ? error.message : String(error),
					},
				],
				runtime,
			};
		}
	}

	getExtensions() {
		return this.extensionsResult;
	}

	getSkills() {
		return { skills: [bundledTic80ctlUsageSkill], diagnostics: [] };
	}

	getPrompts() {
		return { prompts: [], diagnostics: [] };
	}

	getThemes() {
		return { themes: [], diagnostics: [] };
	}

	getAgentsFiles() {
		return { agentsFiles: [] };
	}

	getSystemPrompt() {
		return undefined;
	}

	getAppendSystemPrompt() {
		return [];
	}

	extendResources() {}
}

export function createBrowserResourceLoader(): ResourceLoader {
	return new BrowserResourceLoader();
}
