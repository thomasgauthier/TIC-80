import * as path from "path";

const MAX_OUTPUT_CHARS = 12000;
const MAX_NOTIFICATION_LINES = 6;
const MAX_BASH_LINT_FILES = 20;
const SNAPSHOT_IGNORED_DIRS = new Set([".git", "node_modules", ".pi", ".local", "playtest"]);
const bashSnapshots = new Map<string, Map<string, string>>();

const PLAYTEST_MARKER_RE = /^\s*--\s*tic80ctl:\s*playtest-script\s*$/i;
const SCRIPT_CART_HEADER_RE = /^\s*--\s*script:\s*/im;
const SCRIPT_CART_SECTION_RE = /^\s*--\s*<\/?[A-Z0-9]+>\s*$/m;
const CART_CALLBACK_RE = /^\s*function\s+(TIC|BOOT|SCN|OVR|BDR|MENU)\s*\(/m;
const PLAYTEST_COMMENT_RE = /^\s*--\s*playtest script\b/im;
const PLAYTEST_API_RE = /\b(frameadvance|set_input|end_episode|log)\s*\(/;

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

function resolveFsPath(ctx: any, filePath: string): string {
	if (!ctx?.fs || typeof ctx.fs.resolvePath !== "function") {
		throw new Error("tic80ctl lint extension requires ctx.fs.resolvePath()");
	}
	return ctx.fs.resolvePath(ctx.cwd, filePath);
}

async function readUtf8(fs: any, filePath: string): Promise<string> {
	const value = await fs.readFile(filePath, "utf8");
	if (typeof value === "string") return value;
	return new TextDecoder().decode(value);
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

function detectPlaytestMarker(text: string): boolean {
	let nonEmpty = 0;
	for (const line of text.split(/\r?\n/)) {
		if (line.trim().length === 0) continue;
		nonEmpty += 1;
		if (PLAYTEST_MARKER_RE.test(line)) return true;
		if (nonEmpty >= 8) break;
	}
	return false;
}

function pathLooksLikePlaytest(filePath: string): boolean {
	const normalized = filePath.replace(/\\/g, "/").toLowerCase();
	const base = path.basename(normalized);
	if (/\/playtest\/episode_[^/]+\/script\.lua$/.test(normalized)) return true;
	if (base.startsWith("playtest")) return true;
	if (base.startsWith("episode")) return true;
	if (base.endsWith("_episode.lua")) return true;
	return false;
}

function classifyLuaText(filePath: string, text: string) {
	const explicitPlaytest = detectPlaytestMarker(text);
	const hasCartHeader = SCRIPT_CART_HEADER_RE.test(text);
	const hasCartSection = SCRIPT_CART_SECTION_RE.test(text);
	const cartCallback = CART_CALLBACK_RE.exec(text);
	const hasPlaytestComment = PLAYTEST_COMMENT_RE.test(text);
	const hasPlaytestApi = PLAYTEST_API_RE.test(text);
	const playtestByPath = pathLooksLikePlaytest(filePath);

	if (explicitPlaytest) {
		return {
			kind: "playtest_script" as const,
			subcommand: "lint-playtest-script" as const,
			reason: "explicit `-- tic80ctl: playtest-script` marker",
		};
	}

	if (hasCartHeader) {
		return {
			kind: "script_cart" as const,
			subcommand: "lint-cart" as const,
			reason: "script-cart header `-- script:`",
		};
	}

	if (hasCartSection) {
		return {
			kind: "script_cart" as const,
			subcommand: "lint-cart" as const,
			reason: "tagged TIC-80 cart section like `<PALETTE>`",
		};
	}

	if (cartCallback) {
		return {
			kind: "script_cart" as const,
			subcommand: "lint-cart" as const,
			reason: `cart callback function ${cartCallback[1]}()`,
		};
	}

	if (hasPlaytestApi) {
		return {
			kind: "playtest_script" as const,
			subcommand: "lint-playtest-script" as const,
			reason: "playtest API call like frameadvance()/set_input()/end_episode()/log()",
		};
	}

	if (playtestByPath && hasPlaytestComment) {
		return {
			kind: "playtest_script" as const,
			subcommand: "lint-playtest-script" as const,
			reason: "playtest-oriented filename plus playtest comment",
		};
	}

	if (playtestByPath) {
		return {
			kind: "playtest_script" as const,
			subcommand: "lint-playtest-script" as const,
			reason: "playtest-oriented filename",
		};
	}

	return {
		kind: "script_cart" as const,
		subcommand: "lint-cart" as const,
		reason: "defaulted to cart lint",
	};
}

async function classifyLuaFile(ctx: any, filePath: string, absolutePath: string) {
	try {
		const text = await readUtf8(ctx.fs, absolutePath);
		return {
			path: filePath,
			absolutePath,
			...classifyLuaText(filePath, text),
		};
	} catch {
		return {
			path: filePath,
			absolutePath,
			kind: "script_cart" as const,
			subcommand: "lint-cart" as const,
			reason: "failed to read file for classification; defaulted to cart lint",
		};
	}
}

function lintLabel(subcommand: "lint-cart" | "lint-playtest-script"): string {
	return subcommand === "lint-cart" ? "tic80ctl lint-cart" : "tic80ctl lint-playtest-script";
}

async function checkTic80ctlLintCommand(pi: any, subcommand: "lint-cart" | "lint-playtest-script") {
	const supports = await pi.exec("bash", ["-lc", `tic80ctl help ${JSON.stringify(subcommand)} >/dev/null 2>&1`]);
	if (supports.code === 0) return "ready" as const;

	const exists = await pi.exec("bash", ["-lc", "tic80ctl --help >/dev/null 2>&1"]);
	if (exists.code === 0) return "unsupported" as const;

	return "missing" as const;
}

async function runTic80ctlLint(
	pi: any,
	cwd: string,
	filePath: string,
	subcommand: "lint-cart" | "lint-playtest-script",
	kind: "script_cart" | "playtest_script",
) {
	const lint = await pi.exec("bash", [
		"-lc",
		`cd ${JSON.stringify(cwd)} && tic80ctl ${subcommand} ${JSON.stringify(filePath)}`,
	]);
	const stdout = lint.stdout?.trim() ?? "";
	const stderr = lint.stderr?.trim() ?? "";
	const rawOutput = [stdout, stderr].filter(Boolean).join("\n");

	return {
		file: filePath,
		subcommand,
		kind,
		ok: lint.code === 0,
		exitCode: lint.code,
		stdout,
		stderr,
		output: rawOutput || `(tic80ctl ${subcommand} exited ${lint.code} with no output)`,
	};
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

async function buildLintGroupSummary(pi: any, cwd: string, classifiedFiles: any[], reason: string) {
	const firstFile = classifiedFiles[0];
	if (!firstFile) {
		return {
			content: [],
			details: { files: [], trigger: reason },
			notifyMessage: "[tic80ctl lint] no files to lint",
			notifyLevel: "info" as const,
		};
	}

	const subcommand = firstFile.subcommand;
	const filesText = classifiedFiles.map((file) => file.path).join(", ");
	const label = lintLabel(subcommand);
	const availability = await checkTic80ctlLintCommand(pi, subcommand);

	if (availability === "missing") {
		return {
			content: [
				{
					type: "text" as const,
					text: `\n\n[${label}] Skipped lint for ${filesText}: tic80ctl is not installed or not on PATH.`,
				},
			],
			details: {
				subcommand,
				skipped: true,
				reason: "tic80ctl not installed",
				files: classifiedFiles,
				trigger: reason,
			},
			notifyMessage: `[${label}] skipped ${filesText} (tic80ctl not installed)`,
			notifyLevel: "warning" as const,
		};
	}

	if (availability === "unsupported") {
		return {
			content: [
				{
					type: "text" as const,
					text: `\n\n[${label}] Skipped lint for ${filesText}: installed tic80ctl does not support \`${subcommand}\`.`,
				},
			],
			details: {
				subcommand,
				skipped: true,
				reason: `${subcommand} unsupported`,
				files: classifiedFiles,
				trigger: reason,
			},
			notifyMessage: `[${label}] skipped ${filesText} (${subcommand} unsupported)`,
			notifyLevel: "warning" as const,
		};
	}

	const results = [];
	for (const file of classifiedFiles) {
		results.push(await runTic80ctlLint(pi, cwd, file.path, file.subcommand, file.kind));
	}

	const failures = results.filter((result) => !result.ok);
	if (failures.length === 0) {
		return {
			content: [
				{
					type: "text" as const,
					text:
						subcommand === "lint-cart"
							? `\n\n[${label}] No TIC-80 cart-structure issues in ${filesText}.`
							: `\n\n[${label}] No playtest-script issues in ${filesText}.`,
				},
			],
			details: {
				subcommand,
				files: classifiedFiles,
				ok: true,
				results,
				trigger: reason,
			},
			notifyMessage: `[${label}] clean: ${filesText}`,
			notifyLevel: "info" as const,
		};
	}

	const output = truncate(
		failures
			.map((result) => `${result.file}\n${result.output}`)
			.join("\n\n"),
	);

	return {
		content: [
			{
				type: "text" as const,
				text:
					subcommand === "lint-cart"
						? `\n\n[${label}] TIC-80 cart-structure issues in ${filesText}:\n${output}`
						: `\n\n[${label}] Playtest-script issues in ${filesText}:\n${output}`,
			},
		],
		details: {
			subcommand,
			files: classifiedFiles,
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
	const classified = await Promise.all(
		filePaths.map(async (filePath) => {
			const absolutePath = resolveFsPath(ctx, filePath);
			return classifyLuaFile(ctx, filePath, absolutePath);
		}),
	);

	const groups = new Map<string, any[]>();
	for (const file of classified) {
		const bucket = groups.get(file.subcommand) ?? [];
		bucket.push(file);
		groups.set(file.subcommand, bucket);
	}

	const groupSummaries = [];
	for (const subcommand of ["lint-cart", "lint-playtest-script"] as const) {
		const files = groups.get(subcommand);
		if (!files || files.length === 0) continue;
		groupSummaries.push(await buildLintGroupSummary(pi, ctx.cwd, files, reason));
	}

	const classificationLines = classified
		.map((file) => `${file.path}: ${file.kind} (${file.reason})`)
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
				files: classified,
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
