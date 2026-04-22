export function createInterface(_options: { input?: unknown; output?: unknown; terminal?: boolean; prompt?: string }): {
	on(event: string, cb: (...args: unknown[]) => void): unknown;
	close(): void;
	question(query: string, cb: (answer: string) => void): void;
} {
	return {
		on: () => null,
		close: () => {},
		question: (_query: string, cb: (answer: string) => void) => {
			cb("");
		},
	};
}
