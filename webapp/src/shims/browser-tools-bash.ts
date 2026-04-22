/// <reference lib="dom" />

import { Buffer } from "buffer";

export function createLocalBashOperations(): any {
	return {
		exec: async (command: string, _cwd: string, options: any) => {
			const workspace = (window as any).__piBrowserWorkspace;
			if (!workspace) {
				if (options.onData) options.onData(Buffer.from("Browser workspace not initialized\n"));
				return { exitCode: 1 };
			}

			// Strip any shopt alias setup that pi might have prepended
			const lines = command.split("\n");
			const cleanLines = lines.filter((line) => !line.includes("shopt -s expand_aliases"));
			command = cleanLines.join("\n").trim();

			if (!command) {
				return { exitCode: 0 };
			}

			try {
				if (options.onData) options.onData(Buffer.from(`[DEBUG] Executing: ${JSON.stringify(command)}\n`));
				const result = await workspace.exec(command, {
					timeoutMs: options.timeout ? options.timeout * 1000 : undefined,
					signal: options.signal,
				});

				if (options.onData) {
					if (result.stdout) options.onData(Buffer.from(result.stdout));
					if (result.stderr) {
						if (result.stdout && !result.stdout.endsWith("\n")) options.onData(Buffer.from("\n"));
						options.onData(Buffer.from(result.stderr));
					}
				}

				return { exitCode: result.exitCode };
			} catch (err) {
				if (options.onData) options.onData(Buffer.from(`[DEBUG] Caught error: ${String(err)}\n`));
				return { exitCode: 1 };
			}
		},
	};
}
