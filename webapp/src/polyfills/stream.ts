export class Readable {
	private listeners = new Map<string, Set<(...args: unknown[]) => void>>();

	on(event: string, listener: (...args: unknown[]) => void): this {
		let set = this.listeners.get(event);
		if (!set) {
			set = new Set();
			this.listeners.set(event, set);
		}
		set.add(listener);
		return this;
	}

	pipe(_dest: unknown): unknown {
		return null;
	}

	destroy(): void {
		this.listeners.clear();
	}
}

export class Writable {
	write(_data: unknown): boolean {
		return true;
	}

	end(): void {}
}

export class PassThrough extends Readable {}

export class Transform extends Readable {
	push(_data: unknown): boolean {
		return false;
	}
}

export const promises = {
	pipeline: async (..._streams: unknown[]) => {},
};

export default {
	Readable,
	Writable,
	PassThrough,
	Transform,
	promises,
};
