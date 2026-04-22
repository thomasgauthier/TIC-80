export function fileURLToPath(url: string | URL): string {
	if (typeof url === "string") {
		return url.replace("file://", "").replace("file:", "");
	}
	return url.href.replace("file://", "").replace("file:", "");
}

export function pathToFileURL(path: string): URL {
	return new URL(`file://${path}`);
}

export function format(urlObject: Record<string, string>): string {
	return `${urlObject.protocol}//${urlObject.hostname}${urlObject.pathname}`;
}
