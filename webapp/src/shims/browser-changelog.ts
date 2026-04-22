import { getChangelogPath } from "../browser-config.js";

export interface ChangelogEntry {
	major: number;
	minor: number;
	patch: number;
	content: string;
}

export function parseChangelog(): ChangelogEntry[] {
	return [];
}

export function getNewEntries(): ChangelogEntry[] {
	return [];
}

export { getChangelogPath };
