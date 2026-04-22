import type { ResourceLoader } from "../../../../pi-mono/packages/coding-agent/src/core/resource-loader.js";

import { bundledTic80ctlUsageSkill } from "./bundled-skill.js";

export function createBrowserResourceLoader(): ResourceLoader {
	return {
		getExtensions() {
			return {
				extensions: [],
				errors: [],
				runtime: { pendingProviderRegistrations: [], flagValues: new Map<string, boolean | string>() },
			};
		},
		getSkills() {
			return { skills: [bundledTic80ctlUsageSkill], diagnostics: [] };
		},
		getPrompts() {
			return { prompts: [], diagnostics: [] };
		},
		getThemes() {
			return { themes: [], diagnostics: [] };
		},
		getAgentsFiles() {
			return { agentsFiles: [] };
		},
		getSystemPrompt() {
			return undefined;
		},
		getAppendSystemPrompt() {
			return [];
		},
		extendResources() {},
		async reload() {},
	} as unknown as ResourceLoader;
}
