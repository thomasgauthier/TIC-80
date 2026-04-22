/// <reference lib="dom" />

import { defineCommand } from "just-bash/browser";
import type { CustomCommand } from "just-bash/browser";

/**
 * Interface that the TIC-80 host layer must satisfy for tic80ctl commands.
 * Decouples the just-bash custom command from the coordinator internals.
 */
export interface Tic80CtlRunner {
	runCommand(argv: string[]): Promise<{ stdout: string; stderr: string; exitCode: number }>;
	status(): Promise<{ bound: boolean; initialized: boolean; [key: string]: unknown }>;
	/**
	 * Start a TIC-80 session: create target, bind transport, MCP initialize,
	 * and switch the workspace filesystem to MCP-backed (McpFs).
	 */
	startAndBind(kind: "iframe" | "popup"): Promise<unknown>;
	/**
	 * Stop the session, close the target, and reset the workspace filesystem
	 * back to in-memory.
	 */
	stopAndReset(): Promise<void>;
}

const HELP_TEXT = [
	"usage: tic80ctl <subcommand> [args...]",
	"",
	"Session management:",
	"  start [iframe|popup]     Start TIC-80 session (default: iframe)",
	"  stop                     Stop session and reset filesystem",
	"  status                   Show session status",
	"",
	"Runtime commands:",
	"  cmd <command>            Run a TIC-80 console command",
	"  eval <expression>        Evaluate a Lua expression",
	"  load <cart>              Load a cartridge",
	"  run                      Run the loaded cartridge",
	"",
	"Media:",
	"  screenshot [path]        Capture a screenshot",
	"  playtest --script-file <path> [--timeout <seconds>]",
	"                           Run a playtest episode",
	"",
	"Audio & assets:",
	"  sfx ...                  Sound effect operations",
	"  music ...                Music operations",
	"  sprite ...               Sprite operations",
	"  map ...                  Map operations",
	"",
	"Validation:",
	"  lint-cart [path]         Lint a cartridge",
	"  lint-playtest-script [path]  Lint a playtest script",
].join("\n");

const ADMIN_COMMANDS = new Set(["start", "stop", "status", "help", "--help", "-h"]);

/**
 * Create the `tic80ctl` custom command for just-bash.
 *
 * Intercepts `start`/`stop` to trigger the full session lifecycle
 * (target creation + filesystem switch) and delegates all other
 * subcommands to the WASM tic80ctl core.
 */
export function createTic80ctlCommand(runner: Tic80CtlRunner): CustomCommand {
	return defineCommand("tic80ctl", async (args, _ctx) => {
		// No args → help
		if (args.length === 0) {
			return { stdout: "", stderr: HELP_TEXT + "\n", exitCode: 1 };
		}

		const subcommand = args[0];

		// --help / -h / help
		if (subcommand === "--help" || subcommand === "-h" || subcommand === "help") {
			return { stdout: HELP_TEXT + "\n", stderr: "", exitCode: 0 };
		}

		// start → full bind flow
		if (subcommand === "start") {
			const kindArg = args[1];
			const kind: "iframe" | "popup" =
				kindArg === "popup" ? "popup" : "iframe";

			try {
				const result = await runner.startAndBind(kind);
				// startAndBind returns the coordinator.start() result
				// which has { stdout, stderr, exitCode } from the WASM layer
				if (result && typeof result === "object" && "exitCode" in result) {
					const r = result as { stdout?: string; stderr?: string; exitCode?: number };
					return {
						stdout: r.stdout ?? "",
						stderr: r.stderr ?? "",
						exitCode: r.exitCode ?? 0,
					};
				}
				return { stdout: `Session started (${kind}).\n`, stderr: "", exitCode: 0 };
			} catch (error) {
				return {
					stdout: "",
					stderr: `tic80ctl start failed: ${error instanceof Error ? error.message : String(error)}\n`,
					exitCode: 1,
				};
			}
		}

		// stop → full unbind flow
		if (subcommand === "stop") {
			try {
				await runner.stopAndReset();
				return { stdout: "Session stopped. Filesystem reset to in-memory.\n", stderr: "", exitCode: 0 };
			} catch (error) {
				return {
					stdout: "",
					stderr: `tic80ctl stop failed: ${error instanceof Error ? error.message : String(error)}\n`,
					exitCode: 1,
				};
			}
		}

		// All other subcommands require an active session
		const status = await runner.status();
		if (!status.bound && !ADMIN_COMMANDS.has(subcommand)) {
			return {
				stdout: "",
				stderr:
					"tic80ctl: no TIC-80 session bound. Run `tic80ctl start` first, or use the host panel buttons.\n",
				exitCode: 1,
			};
		}

		// Delegate to WASM tic80ctl core via coordinator
		const result = await runner.runCommand(args);
		return {
			stdout: result.stdout,
			stderr: result.stderr,
			exitCode: result.exitCode,
		};
	});
}
