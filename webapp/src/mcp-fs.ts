/// <reference lib="dom" />

import type { IFileSystem } from "just-bash/browser";

// Inline the types that just-bash/browser doesn't re-export.
// These are stable and match dist/fs/interface.d.ts exactly.
type BufferEncoding = "utf8" | "utf-8" | "ascii" | "binary" | "base64" | "hex" | "latin1";
interface ReadFileOptions { encoding?: BufferEncoding | null }
interface WriteFileOptions { encoding?: BufferEncoding }
interface MkdirOptions { recursive?: boolean }
interface RmOptions { recursive?: boolean; force?: boolean }
interface CpOptions { recursive?: boolean }
interface DirentEntry { name: string; isFile: boolean; isDirectory: boolean; isSymbolicLink: boolean }
interface FsStat { isFile: boolean; isDirectory: boolean; isSymbolicLink: boolean; mode: number; size: number; mtime: Date }

/**
 * MCP callTool function shape — matches the transport controller's callTool.
 * Accepts a tool name and arguments object; returns the MCP result object
 * with { content: [{type,text}], isError }.
 */
type McpCallTool = (name: string, args: Record<string, unknown>) => Promise<unknown>;

/**
 * Parse helper for MCP fs tool responses.  Every fs_* tool returns
 * `{ content: [{type:"text", text:string}], isError?: boolean }`.
 * This helper extracts the text, JSON-parses it when expected, and
 * throws on isError.
 */
async function callAndParse(
	callTool: McpCallTool,
	tool: string,
	args: Record<string, unknown>,
	parseJson = false,
): Promise<string> {
	const raw = await callTool(tool, args);
	const result = raw as {
		content?: Array<{ type: string; text: string }>;
		isError?: boolean;
	} | null;

	if (!result || !Array.isArray(result.content) || result.content.length === 0) {
		throw new Error(`MCP fs tool ${tool} returned empty result`);
	}

	const text = result.content[0].text;
	if (result.isError) {
		throw new Error(text);
	}

	return text;
}

function parseBool(text: string): boolean {
	return text === "true";
}

function parseFsStat(text: string): FsStat {
	const obj = JSON.parse(text) as {
		type: string;
		size: number;
		mode: number;
		mtime: number | null;
	};
	const isDir = obj.type === "directory";
	const isFile = obj.type === "file";
	const isSymlink = obj.type === "symlink";
	return {
		isFile,
		isDirectory: isDir,
		isSymbolicLink: isSymlink,
		mode: obj.mode,
		size: obj.size,
		mtime: obj.mtime != null ? new Date(obj.mtime) : new Date(0),
	};
}

/**
 * IFileSystem implementation backed by TIC-80 MCP fs_* tools.
 *
 * Each method issues a `tools/call` to the TIC-80 Emscripten target
 * over the postMessage bridge.  The target's prejs.js intercepts
 * `fs_*` tool calls and operates directly on Module.FS (IDBFS).
 */
export class McpFs implements IFileSystem {
	private callTool: McpCallTool;

	constructor(callTool: McpCallTool) {
		this.callTool = callTool;
	}

	// ── read ──────────────────────────────────────────────────────

	async readFile(path: string, options?: ReadFileOptions | BufferEncoding): Promise<string> {
		return callAndParse(this.callTool, "fs_read_file", { path });
	}

	async readFileBuffer(path: string): Promise<Uint8Array> {
		const b64 = await callAndParse(this.callTool, "fs_read_file", {
			path,
			encoding: "base64",
		});
		const bin = atob(b64);
		const buf = new Uint8Array(bin.length);
		for (let i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
		return buf;
	}

	// ── write ─────────────────────────────────────────────────────

	async writeFile(
		path: string,
		content: string | Uint8Array,
		_options?: WriteFileOptions | BufferEncoding,
	): Promise<void> {
		if (typeof content === "string") {
			await callAndParse(this.callTool, "fs_write_file", { path, content });
		} else {
			let bin = "";
			for (let i = 0; i < content.length; i++)
				bin += String.fromCharCode(content[i]);
			await callAndParse(this.callTool, "fs_write_file", {
				path,
				content: btoa(bin),
				encoding: "base64",
			});
		}
	}

	async appendFile(
		path: string,
		content: string | Uint8Array,
		_options?: WriteFileOptions | BufferEncoding,
	): Promise<void> {
		if (typeof content === "string") {
			await callAndParse(this.callTool, "fs_append_file", { path, content });
		} else {
			let bin = "";
			for (let i = 0; i < content.length; i++)
				bin += String.fromCharCode(content[i]);
			await callAndParse(this.callTool, "fs_append_file", {
				path,
				content: btoa(bin),
				encoding: "base64",
			});
		}
	}

	// ── metadata ──────────────────────────────────────────────────

	async exists(path: string): Promise<boolean> {
		const text = await callAndParse(this.callTool, "fs_exists", { path });
		return parseBool(text);
	}

	async stat(path: string): Promise<FsStat> {
		const text = await callAndParse(this.callTool, "fs_stat", { path });
		return parseFsStat(text);
	}

	async lstat(path: string): Promise<FsStat> {
		const text = await callAndParse(this.callTool, "fs_lstat", { path });
		return parseFsStat(text);
	}

	// ── directories ───────────────────────────────────────────────

	async mkdir(path: string, options?: MkdirOptions): Promise<void> {
		await callAndParse(this.callTool, "fs_mkdir", {
			path,
			recursive: options?.recursive !== false,
		});
	}

	async readdir(path: string): Promise<string[]> {
		const text = await callAndParse(this.callTool, "fs_readdir", { path });
		return JSON.parse(text) as string[];
	}

	async readdirWithFileTypes(path: string): Promise<DirentEntry[]> {
		const text = await callAndParse(this.callTool, "fs_readdir_with_filetypes", { path });
		const raw = JSON.parse(text) as Array<{ name: string; type: string }>;
		return raw.map((e) => ({
			name: e.name,
			isFile: e.type === "file",
			isDirectory: e.type === "directory",
			isSymbolicLink: e.type === "symlink",
		}));
	}

	// ── mutations ─────────────────────────────────────────────────

	async rm(path: string, options?: RmOptions): Promise<void> {
		await callAndParse(this.callTool, "fs_rm", {
			path,
			recursive: !!options?.recursive,
			force: !!options?.force,
		});
	}

	async cp(src: string, dest: string, _options?: CpOptions): Promise<void> {
		await callAndParse(this.callTool, "fs_cp", {
			source: src,
			destination: dest,
		});
	}

	async mv(src: string, dest: string): Promise<void> {
		await callAndParse(this.callTool, "fs_mv", {
			source: src,
			destination: dest,
		});
	}

	async chmod(path: string, mode: number): Promise<void> {
		await callAndParse(this.callTool, "fs_chmod", { path, mode });
	}

	// ── symlinks ──────────────────────────────────────────────────

	async symlink(target: string, linkPath: string): Promise<void> {
		await callAndParse(this.callTool, "fs_symlink", {
			target,
			linkpath: linkPath,
		});
	}

	async link(_existingPath: string, _newPath: string): Promise<void> {
		throw new Error("Hard links are not supported by MCP fs");
	}

	async readlink(path: string): Promise<string> {
		return callAndParse(this.callTool, "fs_readlink", { path });
	}

	async realpath(path: string): Promise<string> {
		return callAndParse(this.callTool, "fs_realpath", { path });
	}

	// ── path helpers ──────────────────────────────────────────────

	resolvePath(base: string, path: string): string {
		if (path.startsWith("/")) return path;

		const parts = (base + "/" + path).split("/");
		const resolved: string[] = [];
		for (const part of parts) {
			if (part === "" || part === ".") continue;
			if (part === "..") {
				if (resolved.length > 0) resolved.pop();
			} else {
				resolved.push(part);
			}
		}
		return "/" + resolved.join("/");
	}

	getAllPaths(): string[] {
		// Synchronous by interface contract but MCP is async.
		// Return empty array; callers that need paths should use
		// the async fs_get_all_paths tool directly.
		return [];
	}

	// ── utimes (best-effort) ──────────────────────────────────────

	async utimes(_path: string, _atime: Date, _mtime: Date): Promise<void> {
		// No MCP fs_utimes tool exists yet — silently succeed
		// to keep the interface contract happy.
	}
}
