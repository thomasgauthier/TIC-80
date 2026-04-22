const fileStore = new Map<string, { content: string; mtime: Date }>();

function _notAvailable(): never {
	throw new Error("Filesystem operations are not available in the browser");
}

export function existsSync(path: string): boolean {
	return fileStore.has(path);
}

export function readFileSync(path: string, _encoding?: string): string | Buffer {
	const entry = fileStore.get(path);
	if (entry) return entry.content;
	throw new Error(`ENOENT: no such file or directory, open '${path}'`);
}

export function writeFileSync(
	path: string,
	data: string | Buffer | Uint8Array,
	_options?: { encoding?: string },
): void {
	fileStore.set(path, {
		content: typeof data === "string" ? data : new TextDecoder().decode(data),
		mtime: new Date(),
	});
}

export function mkdirSync(_path: string, _options?: { recursive?: boolean }): void {}

export function unlinkSync(path: string): void {
	fileStore.delete(path);
}

export function appendFileSync(path: string, data: string | Buffer, _options?: { encoding?: string }): void {
	const existing = fileStore.get(path);
	const content = typeof data === "string" ? data : new TextDecoder().decode(data);
	fileStore.set(path, {
		content: existing ? existing.content + content : content,
		mtime: new Date(),
	});
}

export function readdirSync(_path: string): string[] {
	return [];
}

export function statSync(path: string): { isFile(): boolean; isDirectory(): boolean; mtime: Date; size: number } {
	if (fileStore.has(path)) {
		return {
			isFile: () => true,
			isDirectory: () => false,
			mtime: fileStore.get(path)!.mtime,
			size: fileStore.get(path)!.content.length,
		};
	}
	throw new Error(`ENOENT: no such file or directory, stat '${path}'`);
}

export function renameSync(oldPath: string, newPath: string): void {
	const entry = fileStore.get(oldPath);
	if (entry) {
		fileStore.delete(oldPath);
		fileStore.set(newPath, entry);
	}
}

export function copyFileSync(src: string, dest: string): void {
	const entry = fileStore.get(src);
	if (entry) {
		fileStore.set(dest, { ...entry });
	}
}

export function chmodSync(_path: string, _mode: number): void {}

export function rmSync(path: string, _options?: { recursive?: boolean; force?: boolean }): void {
	fileStore.delete(path);
}

export function watch(
	_path: string,
	_options?: { persistent?: boolean },
	_listener?: (event: string, filename: string | null) => void,
): { close(): void } {
	return { close() {} };
}

export function watchFile(_path: string, _listener: () => void): void {}

export function unwatchFile(_path: string, _listener?: () => void): void {}

export function createReadStream(_path: string): {
	on(event: string, cb: (data?: unknown) => void): unknown;
	pipe(dest: unknown): unknown;
} {
	return { on: () => null, pipe: () => null };
}

export function createWriteStream(_path: string): {
	on(event: string, cb: (data?: unknown) => void): unknown;
	write(data: unknown): void;
	end(): void;
} {
	return { on: () => null, write: () => {}, end: () => {} };
}

export const FSWatcher = class {
	close() {}
};

export function registerFileContent(path: string, content: string): void {
	fileStore.set(path, { content, mtime: new Date() });
}

export function openSync(_path: string, _flags: string | number): number {
	return 1;
}

export function closeSync(_fd: number): void {}

export function readSync(
	_fd: number,
	_buffer: Uint8Array,
	_offset: number,
	_length: number,
	_position: number | null,
): number {
	return 0;
}

export function writeSync(
	_fd: number,
	buffer: Uint8Array | string,
	_offset?: number,
	_length?: number,
	_position?: number,
): number {
	return typeof buffer === "string" ? buffer.length : buffer.length;
}

export function fstatSync(_fd: number): { isFile(): boolean; isDirectory(): boolean; size: number } {
	return { isFile: () => false, isDirectory: () => false, size: 0 };
}

export function realpathSync(path: string): string {
	return path;
}

export function lstatSync(path: string): {
	isFile(): boolean;
	isDirectory(): boolean;
	isSymbolicLink(): boolean;
	mtime: Date;
	size: number;
} {
	if (fileStore.has(path)) {
		return {
			isFile: () => true,
			isDirectory: () => false,
			isSymbolicLink: () => false,
			mtime: fileStore.get(path)!.mtime,
			size: fileStore.get(path)!.content.length,
		};
	}
	throw new Error(`ENOENT: no such file or directory, lstat '${path}'`);
}

export function accessSync(path: string, _mode?: number): void {
	if (!fileStore.has(path)) {
		throw new Error(`ENOENT: no such file or directory, access '${path}'`);
	}
}

export const constants = {
	O_RDONLY: 0,
	O_WRONLY: 1,
	O_RDWR: 2,
	O_CREAT: 64,
	O_TRUNC: 512,
	O_APPEND: 1024,
	F_OK: 0,
	R_OK: 4,
	W_OK: 2,
	X_OK: 1,
};

export type FSWatcher = { close(): void };

export async function readdir(_path: string): Promise<string[]> {
	return [];
}

export async function stat(
	path: string,
): Promise<{ isFile(): boolean; isDirectory(): boolean; size: number; mtime: Date }> {
	if (fileStore.has(path)) {
		const entry = fileStore.get(path)!;
		return {
			isFile: () => true,
			isDirectory: () => false,
			size: entry.content.length,
			mtime: entry.mtime,
		};
	}
	throw new Error(`ENOENT: no such file or directory, stat '${path}'`);
}

export async function readFile(path: string, _encoding?: string): Promise<string | Uint8Array> {
	const entry = fileStore.get(path);
	if (entry) return entry.content;
	throw new Error(`ENOENT: no such file or directory, open '${path}'`);
}

export async function writeFile(path: string, data: string | Uint8Array): Promise<void> {
	fileStore.set(path, {
		content: typeof data === "string" ? data : new TextDecoder().decode(data),
		mtime: new Date(),
	});
}

export async function mkdir(_path: string, _options?: { recursive?: boolean }): Promise<void> {}

export async function unlink(path: string): Promise<void> {
	fileStore.delete(path);
}

export async function rename(oldPath: string, newPath: string): Promise<void> {
	const entry = fileStore.get(oldPath);
	if (entry) {
		fileStore.delete(oldPath);
		fileStore.set(newPath, entry);
	}
}

export async function access(path: string, _mode?: number): Promise<void> {
	if (!fileStore.has(path)) {
		throw new Error(`ENOENT: no such file or directory, access '${path}'`);
	}
}

export async function copyFile(src: string, dest: string): Promise<void> {
	const entry = fileStore.get(src);
	if (entry) {
		fileStore.set(dest, { ...entry });
	}
}

export async function rm(path: string, _options?: { recursive?: boolean; force?: boolean }): Promise<void> {
	fileStore.delete(path);
}

export function readlinkSync(path: string): string {
	return path;
}

export function symlinkSync(_target: string, _path: string, _type?: string): void {}

export function linkSync(_existingPath: string, _newPath: string): void {}

export function truncateSync(_path: string, _len?: number): void {}

export function utimesSync(_path: string, _atime: Date | number, _mtime: Date | number): void {}

export function ftruncateSync(_fd: number, _len?: number): void {}

export function fsyncSync(_fd: number): void {}

export function fdatasyncSync(_fd: number): void {}
