/// <reference lib="dom" />

import { Bash, type CustomCommand, type IFileSystem, InMemoryFs } from "just-bash/browser";

const WORKSPACE_STORAGE_KEY = "pi-browser-tui-workspace-v1";
export const BROWSER_WORKSPACE_CWD = "/workspace";

type SnapshotEntry =
	| {
			path: string;
			type: "directory";
			mode: number;
			mtime: string;
	  }
	| {
			path: string;
			type: "file";
			mode: number;
			mtime: string;
			contentBase64: string;
	  }
	| {
			path: string;
			type: "symlink";
			target: string;
	  };

type WorkspaceSnapshot = {
	version: 1;
	entries: SnapshotEntry[];
};

function bytesToBase64(bytes: Uint8Array): string {
	let binary = "";
	for (const byte of bytes) {
		binary += String.fromCharCode(byte);
	}
	return btoa(binary);
}

function base64ToBytes(base64: string): Uint8Array {
	const binary = atob(base64);
	const bytes = new Uint8Array(binary.length);
	for (let index = 0; index < binary.length; index++) {
		bytes[index] = binary.charCodeAt(index);
	}
	return bytes;
}

function comparePaths(a: string, b: string): number {
	return a.length - b.length || a.localeCompare(b);
}

export function createDefaultFs(): InMemoryFs {
	const fs = new InMemoryFs();
	fs.mkdirSync("/workspace", { recursive: true });
	fs.writeFileSync(
		"/workspace/README.md",
		[
			"# Pi Browser Workspace",
			"",
			"This is a sandboxed browser filesystem backed by just-bash.",
			"Files created with the bash tool persist across tool calls and page reloads.",
		].join("\n"),
	);
	return fs;
}

async function serializeFs(fs: IFileSystem): Promise<WorkspaceSnapshot> {
	const paths = fs
		.getAllPaths()
		.filter((path) => path === BROWSER_WORKSPACE_CWD || path.startsWith(`${BROWSER_WORKSPACE_CWD}/`))
		.sort(comparePaths);

	const entries: SnapshotEntry[] = [];
	for (const path of paths) {
		const stat = await fs.lstat(path);
		if (stat.isDirectory) {
			entries.push({
				path,
				type: "directory",
				mode: stat.mode,
				mtime: stat.mtime.toISOString(),
			});
			continue;
		}

		if (stat.isSymbolicLink) {
			entries.push({
				path,
				type: "symlink",
				target: await fs.readlink(path),
			});
			continue;
		}

		entries.push({
			path,
			type: "file",
			mode: stat.mode,
			mtime: stat.mtime.toISOString(),
			contentBase64: bytesToBase64(await fs.readFileBuffer(path)),
		});
	}

	return { version: 1, entries };
}

async function restoreFs(fs: InMemoryFs, snapshot: WorkspaceSnapshot): Promise<void> {
	const directories = snapshot.entries.filter(
		(entry): entry is Extract<SnapshotEntry, { type: "directory" }> => entry.type === "directory",
	);
	const files = snapshot.entries.filter(
		(entry): entry is Extract<SnapshotEntry, { type: "file" }> => entry.type === "file",
	);
	const symlinks = snapshot.entries.filter(
		(entry): entry is Extract<SnapshotEntry, { type: "symlink" }> => entry.type === "symlink",
	);

	for (const entry of directories.sort((a, b) => comparePaths(a.path, b.path))) {
		await fs.mkdir(entry.path, { recursive: true });
		await fs.chmod(entry.path, entry.mode);
		await fs.utimes(entry.path, new Date(entry.mtime), new Date(entry.mtime));
	}

	for (const entry of files.sort((a, b) => comparePaths(a.path, b.path))) {
		await fs.writeFile(entry.path, base64ToBytes(entry.contentBase64));
		await fs.chmod(entry.path, entry.mode);
		await fs.utimes(entry.path, new Date(entry.mtime), new Date(entry.mtime));
	}

	for (const entry of symlinks.sort((a, b) => comparePaths(a.path, b.path))) {
		await fs.symlink(entry.target, entry.path);
	}
}

function loadSnapshot(): WorkspaceSnapshot | null {
	try {
		const raw = window.localStorage.getItem(WORKSPACE_STORAGE_KEY);
		if (!raw) {
			return null;
		}
		const parsed = JSON.parse(raw) as Partial<WorkspaceSnapshot>;
		if (parsed.version !== 1 || !Array.isArray(parsed.entries)) {
			return null;
		}
		return parsed as WorkspaceSnapshot;
	} catch {
		return null;
	}
}

export class BrowserWorkspace {
	bash: Bash;
	fs: IFileSystem;
	private _persistEnabled = true;
	private _customCommands: CustomCommand[] = [];

	private constructor(bash: Bash, fs?: IFileSystem) {
		this.bash = bash;
		this.fs = fs ?? bash.fs;
	}

	static async create(fs?: IFileSystem, customCommands?: CustomCommand[]): Promise<BrowserWorkspace> {
		const effectiveFs = fs ?? createDefaultFs();
		const shouldRestore = !fs && loadSnapshot();
		if (shouldRestore) {
			await restoreFs(effectiveFs as InMemoryFs, shouldRestore);
		}
		const cmds = customCommands ?? [];
		const bash = new Bash({
			fs: effectiveFs,
			cwd: BROWSER_WORKSPACE_CWD,
			env: {
				HOME: BROWSER_WORKSPACE_CWD,
				PWD: BROWSER_WORKSPACE_CWD,
			},
			customCommands: cmds,
		});
		const workspace = new BrowserWorkspace(bash, effectiveFs);
		workspace._customCommands = cmds;
		if (!fs && !shouldRestore) {
			await workspace.persist();
		}
		return workspace;
	}

	/**
	 * Swap the backing filesystem at runtime.
	 * Used to switch between InMemoryFs (cold start) and McpFs (TIC-80 live).
	 * Re-creates the internal Bash instance so bash.exec() uses the new fs.
	 */
	setFs(newFs: IFileSystem): void {
		this.fs = newFs;
		this._persistEnabled = newFs instanceof InMemoryFs;
		this.bash = new Bash({
			fs: newFs,
			cwd: BROWSER_WORKSPACE_CWD,
			env: {
				HOME: BROWSER_WORKSPACE_CWD,
				PWD: BROWSER_WORKSPACE_CWD,
			},
			customCommands: this._customCommands,
		});
	}

	resolvePath(path: string): string {
		return this.fs.resolvePath(BROWSER_WORKSPACE_CWD, path);
	}

	async readFile(path: string): Promise<string> {
		return this.fs.readFile(this.resolvePath(path), "utf8");
	}

	async readFileBuffer(path: string): Promise<Uint8Array> {
		return this.fs.readFileBuffer(this.resolvePath(path));
	}

	async writeFile(path: string, content: string): Promise<void> {
		const absolutePath = this.resolvePath(path);
		await this.fs.writeFile(absolutePath, content);
		if (this._persistEnabled) await this.persist();
	}

	async mkdir(path: string, options?: { recursive?: boolean }): Promise<void> {
		await this.fs.mkdir(this.resolvePath(path), options);
		if (this._persistEnabled) await this.persist();
	}

	async exists(path: string): Promise<boolean> {
		return this.fs.exists(this.resolvePath(path));
	}

	async exec(
		command: string,
		options?: { timeoutMs?: number; signal?: AbortSignal },
	): Promise<{ stdout: string; stderr: string; exitCode: number }> {
		const abortController = new AbortController();

		const onExternalAbort = () => abortController.abort();
		if (options?.signal) {
			if (options.signal.aborted) return { stdout: "", stderr: "Aborted", exitCode: 1 };
			options.signal.addEventListener("abort", onExternalAbort);
		}

		const timeoutId =
			options?.timeoutMs !== undefined
				? window.setTimeout(() => abortController.abort(), options.timeoutMs)
				: undefined;

		try {
			const result = await this.bash.exec(command, {
				cwd: BROWSER_WORKSPACE_CWD,
				signal: abortController.signal,
				rawScript: true,
			});
			if (this._persistEnabled) await this.persist();
			return {
				stdout: result.stdout,
				stderr: result.stderr,
				exitCode: result.exitCode,
			};
		} finally {
			if (timeoutId !== undefined) {
				window.clearTimeout(timeoutId);
			}
			if (options?.signal) {
				options.signal.removeEventListener("abort", onExternalAbort);
			}
		}
	}

	async persist(): Promise<void> {
		if (!this._persistEnabled) return;
		const paths = this.fs.getAllPaths();
		if (paths.length === 0) return;
		const snapshot = await serializeFs(this.fs);
		window.localStorage.setItem(WORKSPACE_STORAGE_KEY, JSON.stringify(snapshot));
	}

	async listWorkspacePaths(): Promise<string[]> {
		return this.fs
			.getAllPaths()
			.filter((path) => path === BROWSER_WORKSPACE_CWD || path.startsWith(`${BROWSER_WORKSPACE_CWD}/`))
			.sort(comparePaths);
	}
}
