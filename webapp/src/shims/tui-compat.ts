export * from "../../../../../pi-mono/packages/tui/src/browser.js";
export { SettingsList } from "../../../../../pi-mono/packages/tui/src/components/settings-list.js";

import { fuzzyFilter } from "../../../../../pi-mono/packages/tui/src/fuzzy.js";
import { Text } from "../../../../../pi-mono/packages/tui/src/browser.js";

import type {
	AutocompleteItem,
	AutocompleteProvider,
	AutocompleteSuggestions,
	SettingItem,
	SettingsListTheme,
	SlashCommand,
	Terminal,
} from "../../../../../pi-mono/packages/tui/src/index.js";

export type {
	AutocompleteItem,
	AutocompleteProvider,
	AutocompleteSuggestions,
	SettingItem,
	SlashCommand,
	SettingsListTheme,
};

export class CombinedAutocompleteProvider implements AutocompleteProvider {
	constructor(
		readonly _commands: (SlashCommand | AutocompleteItem)[] = [],
		private readonly basePath: string = "/",
		private readonly fdPath: string | null = null,
	) {}

	async getSuggestions(
		lines: string[],
		cursorLine: number,
		cursorCol: number,
		_options: { signal: AbortSignal; force?: boolean },
	): Promise<AutocompleteSuggestions | null> {
		const currentLine = lines[cursorLine] ?? "";
		const textBeforeCursor = currentLine.slice(0, cursorCol).trimStart();
		if (!textBeforeCursor.startsWith("/")) {
			return null;
		}

		const spaceIndex = textBeforeCursor.indexOf(" ");
		if (spaceIndex !== -1) {
			return null;
		}

		const prefix = textBeforeCursor.slice(1);
		const commandItems = this._commands.map((cmd) => ({
			name: "name" in cmd ? cmd.name : cmd.value,
			label: "name" in cmd ? cmd.name : cmd.label,
			description: cmd.description,
		}));
		const filtered = fuzzyFilter(commandItems, prefix, (item) => item.name).map((item) => ({
			value: item.name,
			label: item.label,
			...(item.description && { description: item.description }),
		}));

		if (filtered.length === 0) {
			return null;
		}

		return {
			items: filtered,
			prefix: textBeforeCursor,
		};
	}

	applyCompletion(
		lines: string[],
		cursorLine: number,
		cursorCol: number,
		item: AutocompleteItem,
		prefix: string,
	): {
		lines: string[];
		cursorLine: number;
		cursorCol: number;
	} {
		const line = lines[cursorLine] ?? "";
		const start = Math.max(0, cursorCol - prefix.length);
		const beforePrefix = line.slice(0, start);
		const afterCursor = line.slice(cursorCol);
		const isSlashCommand = prefix.startsWith("/") && beforePrefix.trim() === "" && !prefix.slice(1).includes("/");

		const nextLine = isSlashCommand
			? `${beforePrefix}/${item.value} ${afterCursor}`
			: `${beforePrefix}${item.value}${afterCursor}`;
		const nextLines = [...lines];
		nextLines[cursorLine] = nextLine;
		return {
			lines: nextLines,
			cursorLine,
			cursorCol: isSlashCommand ? beforePrefix.length + item.value.length + 2 : start + item.value.length,
		};
	}

	getBasePath(): string {
		return this.basePath;
	}

	getFdPath(): string | null {
		return this.fdPath;
	}
}

export class ProcessTerminal implements Terminal {
	start(): void {
		throw new Error("ProcessTerminal is unavailable in the browser");
	}

	stop(): void {}

	async drainInput(): Promise<void> {}

	write(): void {}

	get columns(): number {
		return 80;
	}

	get rows(): number {
		return 24;
	}

	get kittyProtocolActive(): boolean {
		return false;
	}

	moveBy(): void {}

	hideCursor(): void {}

	showCursor(): void {}

	clearLine(): void {}

	clearFromCursor(): void {}

	clearScreen(): void {}

	setTitle(): void {}
}

export class Image extends Text {
	constructor(_base64Data: string, mimeType: string, _theme?: unknown, options?: { filename?: string }) {
		super(imageFallback(mimeType, undefined, options?.filename), 1, 0);
	}
}

export function getCapabilities() {
	return {
		supportsImages: false,
		protocol: null,
		cellSize: undefined,
	};
}

export function getImageDimensions(): null {
	return null;
}

export function imageFallback(mimeType: string, _dimensions?: unknown, filename?: string): string {
	return filename ? `[image:${mimeType}:${filename}]` : `[image:${mimeType}]`;
}
