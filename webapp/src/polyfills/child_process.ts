export function spawn(
	_command: string,
	_args?: string[],
	_options?: Record<string, unknown>,
): {
	on(event: string, cb: (code: number | null, signal: string | null) => void): unknown;
	stdout: { on(event: string, cb: (data: string) => void): unknown } | null;
	stderr: { on(event: string, cb: (data: string) => void): unknown } | null;
	kill(signal?: string): void;
	pid: number;
} {
	return {
		on: () => null,
		stdout: null,
		stderr: null,
		kill: () => {},
		pid: -1,
	};
}

export function spawnSync(
	_command: string,
	_args?: string[],
	_options?: Record<string, unknown>,
): {
	pid: number;
	output: string[];
	stdout: string | null;
	stderr: string | null;
	status: number | null;
	signal: string | null;
	error: Error | null;
} {
	return {
		pid: -1,
		output: [],
		stdout: null,
		stderr: null,
		status: 1,
		signal: null,
		error: new Error("child_process is not available in the browser"),
	};
}

export function execSync(_command: string, _options?: Record<string, unknown>): string | Buffer {
	throw new Error("child_process.execSync is not available in the browser");
}

export function execFile(
	_command: string,
	_args?: string[],
	_options?: Record<string, unknown>,
	_callback?: (error: Error | null, stdout: string, stderr: string) => void,
): unknown {
	return null;
}

export function fork(_module: string, _args?: string[], _options?: Record<string, unknown>): unknown {
	return null;
}
