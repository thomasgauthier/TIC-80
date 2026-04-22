export * from "../../../../../pi-mono/packages/tui/src/browser.js";
export { SettingsList } from "../../../../../pi-mono/packages/tui/src/components/settings-list.js";

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

	async getSuggestions(): Promise<AutocompleteSuggestions | null> {
		return null;
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
		const nextLine = `${line.slice(0, start)}${item.value}${line.slice(cursorCol)}`;
		const nextLines = [...lines];
		nextLines[cursorLine] = nextLine;
		return {
			lines: nextLines,
			cursorLine,
			cursorCol: start + item.value.length,
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
