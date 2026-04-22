type Listener = (...args: unknown[]) => void;

class EventEmitter {
	private listeners = new Map<string, Set<Listener>>();

	on(event: string, listener: Listener): this {
		let set = this.listeners.get(event);
		if (!set) {
			set = new Set();
			this.listeners.set(event, set);
		}
		set.add(listener);
		return this;
	}

	once(event: string, listener: Listener): this {
		const wrapper = (...args: unknown[]) => {
			this.removeListener(event, wrapper);
			listener(...args);
		};
		return this.on(event, wrapper);
	}

	removeListener(event: string, listener: Listener): this {
		this.listeners.get(event)?.delete(listener);
		return this;
	}

	emit(event: string, ...args: unknown[]): boolean {
		const set = this.listeners.get(event);
		if (!set || set.size === 0) return false;
		for (const listener of set) {
			listener(...args);
		}
		return true;
	}
}

interface Stdin extends EventEmitter {
	isRaw: boolean;
	setRawMode(_mode: boolean): void;
	setEncoding(_encoding: string): void;
	resume(): Stdin;
	pause(): Stdin;
	readonly readable: true;
}

interface Stdout extends EventEmitter {
	write(data: string): boolean;
	columns: number;
	rows: number;
	readonly writableLength: 0;
}

const stdin: Stdin = Object.assign(new EventEmitter(), {
	isRaw: false,
	setRawMode(_mode: boolean): void {},
	setEncoding(_encoding: string): void {},
	resume(): Stdin {
		return stdin;
	},
	pause(): Stdin {
		return stdin;
	},
	readable: true as const,
});

const stdout: Stdout = Object.assign(new EventEmitter(), {
	write(_data: string): boolean {
		return true;
	},
	columns: 80,
	rows: 24,
	writableLength: 0 as const,
});

const stderr: Stdout = Object.assign(new EventEmitter(), {
	write(_data: string): boolean {
		return true;
	},
	columns: 80,
	rows: 24,
	writableLength: 0 as const,
});

export const process = {
	argv: ["", ""],
	env: {} as Record<string, string | undefined>,
	version: "v20.0.0",
	versions: {} as Record<string, string>,
	platform: "browser" as const,
	pid: 0,
	execPath: "",
	cwd(): string {
		return "/";
	},
	chdir(_dir: string): void {},
	exit(_code?: number): void {
		throw new Error("process.exit() called");
	},
	nextTick(cb: () => void): void {
		queueMicrotask(cb);
	},
	stdin,
	stdout,
	stderr,
	kill(_pid: number, _signal: string | number): void {},
	on(): never {
		return null as never;
	},
	once(): never {
		return null as never;
	},
	removeListener(): never {
		return null as never;
	},
};
