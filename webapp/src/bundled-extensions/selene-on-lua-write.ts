import * as path from "path";

import { formatDiagnostics, getNotifyLevel, hasErrors, lintLuaText } from "../selene/selene-runtime.js";

const MAX_OUTPUT_CHARS = 12000;
const MAX_NOTIFICATION_LINES = 6;
const MAX_BASH_LINT_FILES = 20;
const SNAPSHOT_IGNORED_DIRS = new Set([".git", "node_modules", ".pi", ".local", "playtest"]);
const bashSnapshots = new Map<string, Map<string, string>>();

type NotifyLevel = "info" | "warning" | "error";

type LintResult = {
	file: string;
	ok: boolean;
	notifyLevel: NotifyLevel;
	output: string;
	diagnosticCount: number;
	message: string;
	error?: string;
};

function isLuaPath(filePath: unknown): filePath is string {
	return typeof filePath === "string" && filePath.endsWith(".lua");
}

function truncate(text: string, max = MAX_OUTPUT_CHARS): string {
	if (text.length <= max) return text;
	return `${text.slice(0, max)}\n\n[selene output truncated to ${max} chars]`;
}

function summarizeForNotification(title: string, output: string): string {
	const lines = output
		.split("\n")
		.map((line) => line.trimEnd())
		.filter((line) => line.trim().length > 0)
		.slice(0, MAX_NOTIFICATION_LINES);

	if (lines.length === 0) return title;
	return `${title}\n${lines.join("\n")}`;
}

function normalizeRelative(cwd: string, filePath: string): string {
	const absolutePath = path.isAbsolute(filePath) ? filePath : path.join(cwd, filePath);
	return path.relative(cwd, absolutePath) || path.basename(absolutePath);
}

async function lstatSafe(fs: any, filePath: string): Promise<any | null> {
	try {
		return await fs.lstat(filePath);
	} catch {
		return null;
	}
}

async function readdirWithKinds(
	fs: any,
	dir: string,
): Promise<Array<{ name: string; isFile: boolean; isDirectory: boolean; isSymbolicLink: boolean }>> {
	if (typeof fs.readdirWithFileTypes === "function") {
		try {
			const entries = await fs.readdirWithFileTypes(dir);
			return entries.map((entry: any) => ({
				name: entry.name,
				isFile: !!entry.isFile,
				isDirectory: !!entry.isDirectory,
				isSymbolicLink: !!entry.isSymbolicLink,
			}));
		} catch {
			return [];
		}
	}

	try {
		const names = await fs.readdir(dir);
		const results = [];
		for (const name of names) {
			const absolutePath = fs.resolvePath(dir, name);
			const stat = await lstatSafe(fs, absolutePath);
			if (!stat) continue;
			results.push({
				name,
				isFile: !!stat.isFile,
				isDirectory: !!stat.isDirectory,
				isSymbolicLink: !!stat.isSymbolicLink,
			});
		}
		return results;
	} catch {
		return [];
	}
}

async function collectLuaSnapshot(fs: any, root: string): Promise<Map<string, string>> {
	const snapshot = new Map<string, string>();

	async function walk(dir: string): Promise<void> {
		const entries = await readdirWithKinds(fs, dir);

		for (const entry of entries) {
			if (entry.name === "." || entry.name === "..") continue;
			if (entry.isDirectory && SNAPSHOT_IGNORED_DIRS.has(entry.name)) continue;

			const absolutePath = fs.resolvePath(dir, entry.name);

			if (entry.isDirectory) {
				await walk(absolutePath);
				continue;
			}

			if (!entry.isFile || !entry.name.endsWith(".lua")) continue;

			try {
				const stat = await fs.lstat(absolutePath);
				const mtimeMs = stat?.mtime instanceof Date ? stat.mtime.getTime() : 0;
				snapshot.set(absolutePath, `${stat.size}:${mtimeMs}`);
			} catch {
				// Ignore racing file changes.
			}
		}
	}

	await walk(root);
	return snapshot;
}

function diffLuaSnapshots(before: Map<string, string>, after: Map<string, string>, cwd: string) {
	const changes: Array<{ path: string; absolutePath: string; kind: "created" | "modified" | "deleted" }> = [];
	const allPaths = new Set([...before.keys(), ...after.keys()]);

	for (const absolutePath of allPaths) {
		const oldSig = before.get(absolutePath);
		const newSig = after.get(absolutePath);
		if (oldSig === newSig) continue;

		let kind: "created" | "modified" | "deleted";
		if (oldSig == null) kind = "created";
		else if (newSig == null) kind = "deleted";
		else kind = "modified";

		changes.push({
			path: normalizeRelative(cwd, absolutePath),
			absolutePath,
			kind,
		});
	}

	return changes.sort((a, b) => a.path.localeCompare(b.path));
}

function severityRank(level: NotifyLevel): number {
	if (level === "error") return 2;
	if (level === "warning") return 1;
	return 0;
}

function mergeDetails(event: any, extra: Record<string, unknown>) {
	return {
		...(event.details ?? {}),
		...extra,
	};
}

async function lintFile(ctx: any, filePath: string): Promise<LintResult> {
	const absolutePath = ctx.fs.resolvePath(ctx.cwd, filePath);
	const source = await ctx.fs.readFile(absolutePath, "utf8");
	const diagnostics = await lintLuaText(String(source));
	const level = getNotifyLevel(diagnostics);

	if (diagnostics.length === 0) {
		return {
			file: filePath,
			ok: true,
			notifyLevel: "info",
			output: `No lint issues in ${filePath}.`,
			diagnosticCount: 0,
			message: `No lint issues in ${filePath}.`,
		};
	}

	const rendered = formatDiagnostics(filePath, diagnostics);
	return {
		file: filePath,
		ok: !hasErrors(diagnostics),
		notifyLevel: level,
		output: rendered,
		diagnosticCount: diagnostics.length,
		message: `${diagnostics.length} issue${diagnostics.length === 1 ? "" : "s"} in ${filePath}`,
	};
}

async function buildSeleneSummary(ctx: any, filePaths: string[], reason: string) {
	const results: LintResult[] = [];
	for (const filePath of filePaths) {
		try {
			results.push(await lintFile(ctx, filePath));
		} catch (error) {
			results.push({
				file: filePath,
				ok: false,
				notifyLevel: "error",
				output: `${filePath}: ${error instanceof Error ? error.message : String(error)}`,
				diagnosticCount: 0,
				message: `Failed to lint ${filePath}`,
				error: error instanceof Error ? error.message : String(error),
			});
		}
	}

	const filesText = filePaths.join(", ");
	const failing = results.filter((result) => !result.ok || result.diagnosticCount > 0 || result.error);
	let notifyLevel: NotifyLevel = "info";
	for (const result of results) {
		if (severityRank(result.notifyLevel) > severityRank(notifyLevel)) {
			notifyLevel = result.notifyLevel;
		}
	}

	if (failing.length === 0) {
		return {
			content: [
				{
					type: "text" as const,
					text: `\n\n[selene] No lint issues in ${filesText}.`,
				},
			],
			details: {
				selene: {
					files: results,
					trigger: reason,
					ok: true,
				},
			},
			isError: false,
			notifyMessage: `[selene] clean: ${filesText}`,
			notifyLevel: "info" as const,
		};
	}

	const output = truncate(failing.map((result) => result.output).join("\n\n"));
	const hasHardErrors = results.some((result) => result.notifyLevel === "error");
	const label = hasHardErrors ? "Lint issues" : "Lint warnings";

	return {
		content: [
			{
				type: "text" as const,
				text: `\n\n[selene] ${label} in ${filesText}:\n${output}`,
			},
		],
		details: {
			selene: {
				files: results,
				trigger: reason,
				ok: !hasHardErrors,
			},
		},
		isError: hasHardErrors,
		notifyMessage: summarizeForNotification(`[selene] ${filesText}`, output),
		notifyLevel,
	};
}

export default function (pi: any) {
	pi.on("tool_call", async (event: any, ctx: any) => {
		if (event.toolName !== "bash") return;
		if (!ctx?.fs) return;

		bashSnapshots.set(event.toolCallId, await collectLuaSnapshot(ctx.fs, ctx.cwd));
	});

	pi.on("tool_result", async (event: any, ctx: any) => {
		const notify = (message: string, level: NotifyLevel) => {
			if (ctx.hasUI) ctx.ui.notify(message, level);
		};

		if (!ctx?.fs) return;

		if (event.toolName === "bash") {
			const before = bashSnapshots.get(event.toolCallId);
			bashSnapshots.delete(event.toolCallId);
			if (!before) return;

			const after = await collectLuaSnapshot(ctx.fs, ctx.cwd);
			const changed = diffLuaSnapshots(before, after, ctx.cwd);
			const existingLuaFiles = changed
				.filter((change) => change.kind !== "deleted")
				.map((change) => change.path)
				.slice(0, MAX_BASH_LINT_FILES);

			if (existingLuaFiles.length === 0) return;

			const omitted = changed.filter((change) => change.kind !== "deleted").length - existingLuaFiles.length;
			const summary = await buildSeleneSummary(ctx, existingLuaFiles, "bash filesystem diff");
			notify(summary.notifyMessage, summary.notifyLevel);

			const changedSummary = changed.map((change) => `${change.kind}: ${change.path}`).join("\n");
			const omittedText = omitted > 0 ? `\n[selene] Omitted ${omitted} additional changed Lua files.` : "";

			return {
				content: [
					...event.content,
					{
						type: "text" as const,
						text: `\n\n[selene] Bash changed Lua files:\n${changedSummary}${omittedText}`,
					},
					...summary.content,
				],
				isError: summary.isError ?? event.isError,
				details: mergeDetails(event, {
					bashLuaChanges: changed,
					...summary.details,
				}),
			};
		}

		if (event.isError) return;
		if (event.toolName !== "write" && event.toolName !== "edit") return;

		const filePath = event.input?.path;
		if (!isLuaPath(filePath)) return;

		const relativePath = normalizeRelative(ctx.cwd, filePath);
		const summary = await buildSeleneSummary(ctx, [relativePath], event.toolName);
		notify(summary.notifyMessage, summary.notifyLevel);

		return {
			content: [...event.content, ...summary.content],
			isError: summary.isError ?? event.isError,
			details: mergeDetails(event, summary.details),
		};
	});
}
