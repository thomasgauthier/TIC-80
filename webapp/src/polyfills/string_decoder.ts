export class StringDecoder {
	private encoding: string;

	constructor(encoding?: string) {
		this.encoding = encoding ?? "utf8";
	}

	write(buffer: Uint8Array): string {
		return new TextDecoder(this.encoding).decode(buffer);
	}

	end(buffer?: Uint8Array): string {
		if (!buffer) return "";
		return new TextDecoder(this.encoding).decode(buffer);
	}
}
