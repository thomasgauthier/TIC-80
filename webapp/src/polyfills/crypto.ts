export function randomUUID(): string {
	return crypto.randomUUID();
}

export function randomBytes(size: number): Uint8Array {
	const bytes = new Uint8Array(size);
	crypto.getRandomValues(bytes);
	return bytes;
}

export function createHash(_algorithm: string) {
	let data = "";
	return {
		update(input: string) {
			data += input;
			return this;
		},
		digest(encoding?: string): string | Uint8Array {
			const encoder = new TextEncoder();
			const bytes = encoder.encode(data);
			if (encoding === "hex") {
				return Array.from(bytes)
					.map((b) => b.toString(16).padStart(2, "0"))
					.join("");
			}
			return bytes;
		},
	};
}

export function createHmac(_algorithm: string, _key: string | Uint8Array) {
	return {
		update(_input: string) {
			return this;
		},
		digest(_encoding?: string): string {
			return "";
		},
	};
}
