type Listener = (...args: unknown[]) => void;

export class EventEmitter {
	private _listeners = new Map<string, Set<Listener>>();
	private maxListeners = 10;

	on(event: string, listener: Listener): this {
		let set = this._listeners.get(event);
		if (!set) {
			set = new Set();
			this._listeners.set(event, set);
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
		this._listeners.get(event)?.delete(listener);
		return this;
	}

	removeAllListeners(event?: string): this {
		if (event) {
			this._listeners.delete(event);
		} else {
			this._listeners.clear();
		}
		return this;
	}

	listenerCount(event: string): number {
		return this._listeners.get(event)?.size ?? 0;
	}

	emit(event: string, ...args: unknown[]): boolean {
		const set = this._listeners.get(event);
		if (!set || set.size === 0) return false;
		for (const listener of [...set]) {
			listener(...args);
		}
		return true;
	}

	prependListener(event: string, listener: Listener): this {
		return this.on(event, listener);
	}

	prependOnceListener(event: string, listener: Listener): this {
		return this.once(event, listener);
	}

	setMaxListeners(n: number): this {
		this.maxListeners = n;
		return this;
	}

	getMaxListeners(): number {
		return this.maxListeners;
	}

	eventNames(): string[] {
		return [...this._listeners.keys()];
	}

	listeners(event: string): Listener[] {
		return [...(this._listeners.get(event) ?? [])];
	}

	rawListeners(event: string): Listener[] {
		return [...(this._listeners.get(event) ?? [])];
	}

	off = EventEmitter.prototype.removeListener;
	addListener = EventEmitter.prototype.on;
}

export const defaultMaxListeners = 10;
