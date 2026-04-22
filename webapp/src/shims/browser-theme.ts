import type { EditorTheme, MarkdownTheme, SelectListTheme, SettingsListTheme } from "@mariozechner/pi-tui";

const reset = "\x1b[0m";
const boldAnsi = (text: string): string => `\x1b[1m${text}${reset}`;
const italicAnsi = (text: string): string => `\x1b[3m${text}${reset}`;
const underlineAnsi = (text: string): string => `\x1b[4m${text}${reset}`;
const strikeAnsi = (text: string): string => `\x1b[9m${text}${reset}`;

const fgCodes = {
	accent: "\x1b[36m",
	border: "\x1b[90m",
	borderAccent: "\x1b[36m",
	borderMuted: "\x1b[90m",
	success: "\x1b[32m",
	error: "\x1b[31m",
	warning: "\x1b[33m",
	muted: "\x1b[90m",
	dim: "\x1b[2m",
	text: "\x1b[37m",
	thinkingText: "\x1b[90m",
	userMessageText: "\x1b[37m",
	customMessageText: "\x1b[37m",
	customMessageLabel: "\x1b[36m",
	toolTitle: "\x1b[36m",
	toolOutput: "\x1b[37m",
	mdHeading: "\x1b[36m",
	mdLink: "\x1b[36m",
	mdLinkUrl: "\x1b[90m",
	mdCode: "\x1b[33m",
	mdCodeBlock: "\x1b[37m",
	mdCodeBlockBorder: "\x1b[90m",
	mdQuote: "\x1b[90m",
	mdQuoteBorder: "\x1b[90m",
	mdHr: "\x1b[90m",
	mdListBullet: "\x1b[36m",
	toolDiffAdded: "\x1b[32m",
	toolDiffRemoved: "\x1b[31m",
	toolDiffContext: "\x1b[90m",
	syntaxComment: "\x1b[90m",
	syntaxKeyword: "\x1b[35m",
	syntaxFunction: "\x1b[36m",
	syntaxVariable: "\x1b[37m",
	syntaxString: "\x1b[33m",
	syntaxNumber: "\x1b[32m",
	syntaxType: "\x1b[34m",
	syntaxOperator: "\x1b[35m",
	syntaxPunctuation: "\x1b[90m",
	thinkingOff: "\x1b[90m",
	thinkingMinimal: "\x1b[36m",
	thinkingLow: "\x1b[36m",
	thinkingMedium: "\x1b[33m",
	thinkingHigh: "\x1b[31m",
	thinkingXhigh: "\x1b[31m",
	bashMode: "\x1b[33m",
} as const;

const bgCodes = {
	selectedBg: "\x1b[48;5;236m",
	userMessageBg: "\x1b[49m",
	customMessageBg: "\x1b[49m",
	toolPendingBg: "\x1b[49m",
	toolSuccessBg: "\x1b[49m",
	toolErrorBg: "\x1b[49m",
} as const;

export type ThemeColor = keyof typeof fgCodes;
type ThemeBg = keyof typeof bgCodes;

export class Theme {
	fg(color: ThemeColor, text: string): string {
		return `${fgCodes[color]}${text}\x1b[39m`;
	}

	bg(color: ThemeBg, text: string): string {
		return `${bgCodes[color]}${text}\x1b[49m`;
	}

	bold(text: string): string {
		return boldAnsi(text);
	}

	italic(text: string): string {
		return italicAnsi(text);
	}

	underline(text: string): string {
		return underlineAnsi(text);
	}

	strikethrough(text: string): string {
		return strikeAnsi(text);
	}

	inverse(text: string): string {
		return `\x1b[7m${text}${reset}`;
	}

	getThinkingBorderColor(level: "off" | "minimal" | "low" | "medium" | "high" | "xhigh"): (text: string) => string {
		const key =
			level === "xhigh"
				? "thinkingXhigh"
				: level === "high"
					? "thinkingHigh"
					: level === "medium"
						? "thinkingMedium"
						: level === "low"
							? "thinkingLow"
							: level === "minimal"
								? "thinkingMinimal"
								: "thinkingOff";
		return (text: string) => this.fg(key, text);
	}

	getBashModeBorderColor(): (text: string) => string {
		return (text: string) => this.fg("bashMode", text);
	}
}

let currentTheme = new Theme();
let onChange: (() => void) | undefined;

export const theme: Theme = new Proxy(new Theme(), {
	get(_target, prop) {
		return currentTheme[prop as keyof Theme];
	},
});

export function setRegisteredThemes(): void {}

export function initTheme(): void {
	currentTheme = new Theme();
}

export function setTheme(): { success: true } {
	currentTheme = new Theme();
	onChange?.();
	return { success: true };
}

export function setThemeInstance(instance: Theme): void {
	currentTheme = instance;
	onChange?.();
}

export function onThemeChange(callback: () => void): void {
	onChange = callback;
}

export function stopThemeWatcher(): void {}

export function getThemeByName(): Theme {
	return currentTheme;
}

export function getAvailableThemes(): string[] {
	return ["dark"];
}

export function getAvailableThemesWithPaths(): Array<{ name: string; path: string | undefined }> {
	return [{ name: "dark", path: undefined }];
}

export function highlightCode(code: string): string[] {
	return code.split("\n").map((line) => theme.fg("mdCodeBlock", line));
}

export function getLanguageFromPath(filePath: string): string | undefined {
	const extension = filePath.split(".").pop()?.toLowerCase();
	if (!extension) {
		return undefined;
	}
	return extension;
}

export function getMarkdownTheme(): MarkdownTheme {
	return {
		heading: (text) => theme.fg("mdHeading", text),
		link: (text) => theme.fg("mdLink", text),
		linkUrl: (text) => theme.fg("mdLinkUrl", text),
		code: (text) => theme.fg("mdCode", text),
		codeBlock: (text) => theme.fg("mdCodeBlock", text),
		codeBlockBorder: (text) => theme.fg("mdCodeBlockBorder", text),
		quote: (text) => theme.fg("mdQuote", text),
		quoteBorder: (text) => theme.fg("mdQuoteBorder", text),
		hr: (text) => theme.fg("mdHr", text),
		listBullet: (text) => theme.fg("mdListBullet", text),
		bold: (text) => theme.bold(text),
		italic: (text) => theme.italic(text),
		underline: (text) => theme.underline(text),
		strikethrough: (text) => theme.strikethrough(text),
		highlightCode: (code) => highlightCode(code),
	};
}

export function getSelectListTheme(): SelectListTheme {
	return {
		selectedPrefix: (text) => theme.fg("accent", text),
		selectedText: (text) => theme.fg("accent", text),
		description: (text) => theme.fg("muted", text),
		scrollInfo: (text) => theme.fg("muted", text),
		noMatch: (text) => theme.fg("muted", text),
	};
}

export function getEditorTheme(): EditorTheme {
	return {
		borderColor: (text) => theme.fg("borderMuted", text),
		selectList: getSelectListTheme(),
	};
}

export function getSettingsListTheme(): SettingsListTheme {
	return {
		label: (text, selected) => (selected ? theme.fg("accent", text) : text),
		value: (text, selected) => (selected ? theme.fg("accent", text) : theme.fg("muted", text)),
		description: (text) => theme.fg("dim", text),
		cursor: theme.fg("accent", "→ "),
		hint: (text) => theme.fg("dim", text),
	};
}
