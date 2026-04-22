/// <reference lib="dom" />

export async function copyToClipboard(text: string): Promise<void> {
	if (typeof navigator !== "undefined" && navigator.clipboard?.writeText) {
		await navigator.clipboard.writeText(text);
	}
}
