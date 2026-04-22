export class ExtensionRunner {}

export function wrapRegisteredTools(): [] {
	return [];
}

export function wrapRegisteredTool<T>(tool: T): T {
	return tool;
}

export function defineTool<T>(tool: T): T {
	return tool;
}

export async function createExtensionRuntime(): Promise<undefined> {
	return undefined;
}

export async function discoverAndLoadExtensions(): Promise<undefined> {
	return undefined;
}

export async function loadExtensionFromFactory(): Promise<undefined> {
	return undefined;
}

export async function loadExtensions(): Promise<undefined> {
	return undefined;
}
