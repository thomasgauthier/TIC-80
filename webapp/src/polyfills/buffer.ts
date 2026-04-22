const base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

type BufferLike = Uint8Array & {
	toString(encoding?: string): string;
};

function withToString(value: Uint8Array): BufferLike {
	const bufferValue = value as BufferLike;
	bufferValue.toString = (encoding?: string) => {
		if (encoding === "base64") {
			let result = "";
			const bytes = new Uint8Array(bufferValue.buffer, bufferValue.byteOffset, bufferValue.byteLength);
			for (let i = 0; i < bytes.length; i += 3) {
				const b0 = bytes[i]!;
				const b1 = i + 1 < bytes.length ? bytes[i + 1]! : 0;
				const b2 = i + 2 < bytes.length ? bytes[i + 2]! : 0;
				result += base64Chars[b0 >> 2]!;
				result += base64Chars[((b0 & 3) << 4) | (b1 >> 4)]!;
				result += i + 1 < bytes.length ? base64Chars[((b1 & 15) << 2) | (b2 >> 6)]! : "=";
				result += i + 2 < bytes.length ? base64Chars[b2 & 63]! : "=";
			}
			return result;
		}
		if (encoding === "hex") {
			const bytes = new Uint8Array(bufferValue.buffer, bufferValue.byteOffset, bufferValue.byteLength);
			return Array.from(bytes)
				.map((byte) => byte.toString(16).padStart(2, "0"))
				.join("");
		}
		return new TextDecoder().decode(bufferValue);
	};
	return bufferValue;
}

function from(data: string | ArrayLike<number> | ArrayBufferLike, encoding?: string): BufferLike {
	if (data instanceof ArrayBuffer) {
		return withToString(new Uint8Array(data));
	}
	if (ArrayBuffer.isView(data)) {
		return withToString(new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
	}
	if (typeof data === "string") {
		if (encoding === "base64") {
			const binary = atob(data);
			const bytes = new Uint8Array(binary.length);
			for (let i = 0; i < binary.length; i++) {
				bytes[i] = binary.charCodeAt(i);
			}
			return withToString(bytes);
		}
		if (encoding === "hex") {
			const bytes = new Uint8Array(data.length / 2);
			for (let i = 0; i < data.length; i += 2) {
				bytes[i / 2] = parseInt(data.substring(i, i + 2), 16);
			}
			return withToString(bytes);
		}
		const encoder = new TextEncoder();
		return withToString(encoder.encode(data));
	}
	return withToString(new Uint8Array(data as ArrayLike<number>));
}

function alloc(size: number): BufferLike {
	return withToString(new Uint8Array(size));
}

function concat(list: Uint8Array[], totalLength?: number): BufferLike {
	const nextLength = totalLength ?? list.reduce((acc, buf) => acc + buf.length, 0);
	const result = new Uint8Array(nextLength);
	let offset = 0;
	for (const buf of list) {
		result.set(buf, offset);
		offset += buf.length;
	}
	return withToString(result);
}

function isBuffer(obj: unknown): obj is BufferLike {
	return obj instanceof Uint8Array;
}

function byteLength(string: string, encoding?: string): number {
	if (encoding === "base64") {
		return atob(string).length;
	}
	if (encoding === "hex") {
		return string.length / 2;
	}
	return new TextEncoder().encode(string).length;
}

export const Buffer = {
	from,
	alloc,
	concat,
	isBuffer,
	byteLength,
};
