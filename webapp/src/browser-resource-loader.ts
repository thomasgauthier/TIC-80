import { createEventBus } from "../../../../pi-mono/packages/coding-agent/src/core/event-bus.js";
import type { ResourceLoader } from "../../../../pi-mono/packages/coding-agent/src/core/resource-loader.js";

import { bundledExtensionFactories } from "./bundled-extension-runtime.js";
import { bundledExtensionFiles } from "./bundled-extension.js";
import { bundledTic80ctlUsageSkill } from "./bundled-skill.js";
import { createExtensionRuntime, loadExtensionFromFactory } from "./shims/browser-extensions.js";

const bundledExtensionSourceInfoByPath = new Map(bundledExtensionFiles.map((file) => [file.path, file.sourceInfo]));

class BrowserResourceLoader implements ResourceLoader {
	private extensionsResult = {
		extensions: [],
		errors: [] as Array<{ path: string; error: string }>,
		runtime: createExtensionRuntime(),
	};

	async reload(): Promise<void> {
		const runtime = createExtensionRuntime();
		const eventBus = createEventBus();
		const extensions = [] as any[];
		const errors = [] as Array<{ path: string; error: string }>;

		for (const entry of bundledExtensionFactories) {
			try {
				const extension = await loadExtensionFromFactory(entry.factory, entry.path, "/workspace", runtime, eventBus);
				extension.sourceInfo = bundledExtensionSourceInfoByPath.get(entry.path) ?? extension.sourceInfo;
				extensions.push(extension);
			} catch (error) {
				errors.push({
					path: entry.path,
					error: error instanceof Error ? error.message : String(error),
				});
			}
		}

		this.extensionsResult = {
			extensions,
			errors,
			runtime,
		};
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
