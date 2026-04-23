import * as path from "path";

const MAX_OUTPUT_CHARS = 12000;
const MAX_NOTIFICATION_LINES = 6;
const MAX_BASH_LINT_FILES = 20;
const SNAPSHOT_IGNORED_DIRS = new Set([".git", "node_modules", ".pi", ".local", "playtest"]);
const bashSnapshots = new Map<string, Map<string, string>>();

function isLuaPath(filePath: unknown): filePath is string {
	return typeof filePath === "string" && filePath.endsWith(".lua");
}

function truncate(text: string, max = MAX_OUTPUT_CHARS): string {
	if (text.length <= max) return text;
	return `${text.slice(0, max)}\n\n[tic80ctl lint output truncated to ${max} chars]`;
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

async function readdirWithKinds(fs: any, dir: string): Promise<Array<{ name: string; isFile: boolean; isDirectory: boolean; isSymbolicLink: boolean }>> {
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

function lintLabel(subcommand: "lint-cart" | "lint-playtest-script" | "lint-lua-auto"): string {
	if (subcommand === "lint-cart") return "tic80ctl lint-cart";
	if (subcommand === "lint-playtest-script") return "tic80ctl lint-playtest-script";
	return "tic80ctl lint-lua-auto";
}

async function checkTic80ctlLintCommand(pi: any) {
	const supports = await pi.exec("bash", ["-lc", "tic80ctl help lint-lua-auto >/dev/null 2>&1"]);
	if (supports.code === 0) return "ready" as const;

	const exists = await pi.exec("bash", ["-lc", "tic80ctl --help >/dev/null 2>&1"]);
	if (exists.code === 0) return "unsupported" as const;

	return "missing" as const;
}

async function runTic80ctlAutoLint(pi: any, cwd: string, filePath: string) {
	const lint = await pi.exec("bash", [
		"-lc",
		`cd ${JSON.stringify(cwd)} && tic80ctl --json lint-lua-auto ${JSON.stringify(filePath)}`,
	]);
	const stdout = lint.stdout?.trim() ?? "";
	const stderr = lint.stderr?.trim() ?? "";
	const rawOutput = [stdout, stderr].filter(Boolean).join("\n");

	try {
		const payload = JSON.parse(stdout || "{}");
		const subcommand =
			payload.subcommand === "lint-playtest-script"
				? ("lint-playtest-script" as const)
				: payload.subcommand === "lint-cart"
					? ("lint-cart" as const)
					: ("lint-lua-auto" as const);
		const kind = payload.kind === "playtest_script" ? ("playtest_script" as const) : ("script_cart" as const);
		const message = typeof payload.message === "string" && payload.message.length > 0 ? payload.message : lint.code === 0 ? "lint ok" : "lint failed";
		const line = typeof payload.line === "number" ? payload.line : undefined;
		const renderedOutput = lint.code === 0
			? `lint ok: ${filePath}`
			: line !== undefined
				? `lint failed: ${filePath}:${line}: ${message}`
				: `lint failed: ${filePath}: ${message}`;
		return {
			file: filePath,
			subcommand,
			kind,
			reason: typeof payload.reason === "string" && payload.reason.length > 0 ? payload.reason : "defaulted to cart lint",
			ok: lint.code === 0 && payload.ok !== false,
			exitCode: lint.code,
			stdout,
			stderr,
			message,
			line,
			output: renderedOutput,
		};
	} catch {
		return {
			file: filePath,
			subcommand: "lint-lua-auto" as const,
			kind: "script_cart" as const,
			reason: "tic80ctl returned non-JSON output; classification unavailable",
			ok: false,
			exitCode: lint.code,
			stdout,
			stderr,
			message: "failed to parse tic80ctl lint-lua-auto JSON output",
			line: undefined,
			output: rawOutput || "failed to parse tic80ctl lint-lua-auto JSON output",
		};
	}
}

function severityRank(level: "info" | "warning" | "error"): number {
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

function buildLintGroupSummary(results: any[], reason: string) {
	const firstResult = results[0];
	if (!firstResult) {
		return {
			content: [],
			details: { files: [], trigger: reason },
			notifyMessage: "[tic80ctl lint] no files to lint",
			notifyLevel: "info" as const,
		};
	}

	const subcommand = firstResult.subcommand;
	const filesText = results.map((result) => result.file).join(", ");
	const label = lintLabel(subcommand);
	const failures = results.filter((result) => !result.ok);

	if (failures.length === 0) {
		return {
			content: [
				{
					type: "text" as const,
					text:
						subcommand === "lint-playtest-script"
							? `\n\n[${label}] No playtest-script issues in ${filesText}.`
							: `\n\n[${label}] No TIC-80 cart-structure issues in ${filesText}.`,
				},
			],
			details: {
				subcommand,
				files: results,
				ok: true,
				results,
				trigger: reason,
			},
			notifyMessage: `[${label}] clean: ${filesText}`,
			notifyLevel: "info" as const,
		};
	}

	const output = truncate(failures.map((result) => result.output).join("\n\n"));

	return {
		content: [
			{
				type: "text" as const,
				text:
					subcommand === "lint-playtest-script"
						? `\n\n[${label}] Playtest-script issues in ${filesText}:\n${output}`
						: `\n\n[${label}] TIC-80 cart-structure issues in ${filesText}:\n${output}`,
			},
		],
		details: {
			subcommand,
			files: results,
			ok: false,
			results,
			trigger: reason,
		},
		isError: true,
		notifyMessage: summarizeForNotification(`[${label}] ${filesText}`, output),
		notifyLevel: "error" as const,
	};
}

async function buildTic80LintSummary(pi: any, ctx: any, filePaths: string[], reason: string) {
	const availability = await checkTic80ctlLintCommand(pi);
	if (availability === "missing") {
		return {
			content: [
				{
					type: "text" as const,
					text: `\n\n[tic80ctl lint-lua-auto] Skipped lint for ${filePaths.join(", ")}: tic80ctl is not installed or not on PATH.`,
				},
			],
			details: {
				tic80ctlLint: {
					files: [],
					trigger: reason,
					skipped: true,
					reason: "tic80ctl not installed",
				},
			},
			isError: false,
			notifyMessage: `[tic80ctl lint-lua-auto] skipped ${filePaths.join(", ")} (tic80ctl not installed)`,
			notifyLevel: "warning" as const,
		};
	}

	if (availability === "unsupported") {
		return {
			content: [
				{
					type: "text" as const,
					text: `\n\n[tic80ctl lint-lua-auto] Skipped lint for ${filePaths.join(", ")}: installed tic80ctl does not support \`lint-lua-auto\`.`,
				},
			],
			details: {
				tic80ctlLint: {
					files: [],
					trigger: reason,
					skipped: true,
					reason: "lint-lua-auto unsupported",
				},
			},
			isError: false,
			notifyMessage: `[tic80ctl lint-lua-auto] skipped ${filePaths.join(", ")} (lint-lua-auto unsupported)`,
			notifyLevel: "warning" as const,
		};
	}

	const results = [];
	for (const filePath of filePaths) {
		results.push(await runTic80ctlAutoLint(pi, ctx.cwd, filePath));
	}

	const groups = new Map<string, any[]>();
	for (const result of results) {
		const bucket = groups.get(result.subcommand) ?? [];
		bucket.push(result);
		groups.set(result.subcommand, bucket);
	}

	const groupSummaries = [];
	for (const subcommand of ["lint-cart", "lint-playtest-script", "lint-lua-auto"] as const) {
		const files = groups.get(subcommand);
		if (!files || files.length === 0) continue;
		groupSummaries.push(buildLintGroupSummary(files, reason));
	}

	const classificationLines = results
		.map((result) => `${result.file}: ${result.kind} (${result.reason})`)
		.join("\n");

	let notifyLevel: "info" | "warning" | "error" = "info";
	for (const summary of groupSummaries) {
		if (severityRank(summary.notifyLevel) > severityRank(notifyLevel)) {
			notifyLevel = summary.notifyLevel;
		}
	}

	const notifyMessage =
		groupSummaries.length === 1
			? groupSummaries[0].notifyMessage
			: summarizeForNotification("[tic80ctl lint] classified Lua files", classificationLines);

	return {
		content: [
			{
				type: "text" as const,
				text: `\n\n[tic80ctl lint] Classified Lua files:\n${classificationLines}`,
			},
			...groupSummaries.flatMap((summary) => summary.content),
		],
		details: {
			tic80ctlLint: {
				files: results,
				trigger: reason,
				groups: groupSummaries.map((summary) => summary.details),
			},
		},
		isError: groupSummaries.some((summary) => summary.isError),
		notifyMessage,
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
		const notify = (message: string, level: "info" | "warning" | "error") => {
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
			const summary = await buildTic80LintSummary(pi, ctx, existingLuaFiles, "bash filesystem diff");
			notify(summary.notifyMessage, summary.notifyLevel);

			const changedSummary = changed.map((change) => `${change.kind}: ${change.path}`).join("\n");
			const omittedText =
				omitted > 0 ? `\n[tic80ctl lint] Omitted ${omitted} additional changed Lua files.` : "";

			return {
				content: [
					...event.content,
					{
						type: "text" as const,
						text: `\n\n[tic80ctl lint] Bash changed Lua files:\n${changedSummary}${omittedText}`,
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
		const summary = await buildTic80LintSummary(pi, ctx, [relativePath], event.toolName);
		notify(summary.notifyMessage, summary.notifyLevel);

		return {
			content: [...event.content, ...summary.content],
			isError: summary.isError ?? event.isError,
			details: mergeDetails(event, summary.details),
		};
	});
}
