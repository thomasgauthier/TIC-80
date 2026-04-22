import { createSyntheticSourceInfo } from "../../../../pi-mono/packages/coding-agent/src/core/source-info.js";
import type { Skill } from "../../../../pi-mono/packages/coding-agent/src/core/skills.js";

import type { BrowserWorkspace } from "./browser-workspace.js";
import { registerFileContent } from "./polyfills/fs.js";

import skillContent from "./bundled-skills/tic80ctl-usage/SKILL.md?raw";
import scriptedPlaytestGuide from "./bundled-skills/tic80ctl-usage/reference/scripted_playtest_guide.md?raw";
import tic80ApiReference from "./bundled-skills/tic80ctl-usage/reference/tic80_api_reference.md?raw";
import tic80ConsoleAndRuntime from "./bundled-skills/tic80ctl-usage/reference/tic80_console_and_runtime.md?raw";
import tic80ProjectWorkflow from "./bundled-skills/tic80ctl-usage/reference/tic80_project_workflow.md?raw";

const SKILL_BASE_DIR = "/workspace/.pi/skills/tic80ctl-usage";
const SKILL_FILE_PATH = `${SKILL_BASE_DIR}/SKILL.md`;

export const bundledTic80ctlUsageSkill: Skill = {
	name: "tic80ctl-usage",
	description:
		"Use `tic80ctl` to start TIC-80, load and run carts, inspect a live game, edit cartridge content from the shell, and run scripted playtests while building games.",
	filePath: SKILL_FILE_PATH,
	baseDir: SKILL_BASE_DIR,
	sourceInfo: createSyntheticSourceInfo(SKILL_FILE_PATH, {
		source: "local",
		scope: "project",
		baseDir: SKILL_BASE_DIR,
	}),
	disableModelInvocation: false,
};

export const bundledTic80ctlUsageFiles = [
	{ path: SKILL_FILE_PATH, content: skillContent },
	{ path: `${SKILL_BASE_DIR}/reference/scripted_playtest_guide.md`, content: scriptedPlaytestGuide },
	{ path: `${SKILL_BASE_DIR}/reference/tic80_api_reference.md`, content: tic80ApiReference },
	{ path: `${SKILL_BASE_DIR}/reference/tic80_console_and_runtime.md`, content: tic80ConsoleAndRuntime },
	{ path: `${SKILL_BASE_DIR}/reference/tic80_project_workflow.md`, content: tic80ProjectWorkflow },
] as const;

function getParentDir(path: string): string {
	const slashIndex = path.lastIndexOf("/");
	return slashIndex <= 0 ? "/" : path.slice(0, slashIndex);
}

export async function installBundledTic80ctlSkill(workspace: BrowserWorkspace): Promise<void> {
	for (const file of bundledTic80ctlUsageFiles) {
		const dir = getParentDir(file.path);
		await workspace.mkdir(dir, { recursive: true });
		await workspace.writeFile(file.path, file.content);
		registerFileContent(file.path, file.content);
	}
}
