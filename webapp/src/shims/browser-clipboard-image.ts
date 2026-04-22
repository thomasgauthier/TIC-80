export type ClipboardImage = {
	bytes: Uint8Array;
	mimeType: string;
};

export function extensionForImageMimeType(mimeType: string): string | null {
	switch (mimeType.toLowerCase()) {
		case "image/png":
			return "png";
		case "image/jpeg":
			return "jpg";
		case "image/webp":
			return "webp";
		case "image/gif":
			return "gif";
		default:
			return null;
	}
}

export async function readClipboardImage(): Promise<ClipboardImage | null> {
	return null;
}
