const globalObject = globalThis as typeof globalThis & { global?: typeof globalThis };

if (!globalObject.global) {
	globalObject.global = globalThis;
}

export default globalThis;
