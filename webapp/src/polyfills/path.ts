export const sep = "/";
export const delimiter = ":";

export function join(...paths: string[]): string {
	const result = normalize(paths.filter(Boolean).join("/"));
	return result || ".";
}

export function resolve(...paths: string[]): string {
	let resolved = paths.filter(Boolean).join("/");
	if (!resolved.startsWith("/")) {
		resolved = `/${resolved}`;
	}
	return normalize(resolved) || "/";
}

export function dirname(p: string): string {
	const normalized = normalize(p);
	const lastSlash = normalized.lastIndexOf("/");
	if (lastSlash <= 0) return "/";
	return normalize(normalized.substring(0, lastSlash)) || "/";
}

export function basename(p: string, ext?: string): string {
	const normalized = normalize(p);
	const lastSlash = normalized.lastIndexOf("/");
	let name = lastSlash >= 0 ? normalized.substring(lastSlash + 1) : normalized;
	if (ext && name.endsWith(ext)) {
		name = name.substring(0, name.length - ext.length);
	}
	return name;
}

export function extname(p: string): string {
	const lastDot = p.lastIndexOf(".");
	const lastSlash = p.lastIndexOf("/");
	if (lastDot <= lastSlash || lastDot === -1) return "";
	return p.substring(lastDot);
}

export function normalize(p: string): string {
	if (!p) return ".";
	const isAbsolute = p.startsWith("/");
	const parts = p.split("/").filter(Boolean);
	const result: string[] = [];
	for (const part of parts) {
		if (part === ".") continue;
		if (part === "..") {
			if (result.length > 0 && result[result.length - 1] !== "..") {
				result.pop();
			} else if (!isAbsolute) {
				result.push("..");
			}
		} else {
			result.push(part);
		}
	}
	let normalized = result.join("/");
	if (isAbsolute) normalized = `/${normalized}`;
	return normalized || (isAbsolute ? "/" : ".");
}

export function relative(from: string, to: string): string {
	const fromParts = resolve(from).split("/").filter(Boolean);
	const toParts = resolve(to).split("/").filter(Boolean);
	let commonLength = 0;
	while (
		commonLength < fromParts.length &&
		commonLength < toParts.length &&
		fromParts[commonLength] === toParts[commonLength]
	) {
		commonLength++;
	}
	const upParts = fromParts.slice(commonLength).map(() => "..");
	const downParts = toParts.slice(commonLength);
	return [...upParts, ...downParts].join("/") || ".";
}

export function isAbsolute(p: string): boolean {
	return p.startsWith("/");
}
