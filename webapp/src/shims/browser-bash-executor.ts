import { Buffer } from "buffer";
import stripAnsi from "strip-ansi";
import { createLocalBashOperations } from "./browser-tools-bash.js";

export function executeBash(command: string, options?: any): Promise<any> {
	return executeBashWithOperations(command, "/workspace", createLocalBashOperations(), options);
}

export async function executeBashWithOperations(
	command: string,
	cwd: string,
	operations: any,
	options?: any,
): Promise<{
	output: string;
	exitCode: number | undefined;
	cancelled: boolean;
	truncated: boolean;
	fullOutputPath?: string;
}> {
	const outputChunks: string[] = [];
	let outputBytes = 0;
	// We'll use 50KB (DEFAULT_MAX_BYTES) and double it for the rolling buffer
	const maxOutputBytes = 50 * 1024 * 2;

	const decoder = new TextDecoder();

	const onData = (data: Buffer) => {
		// Just strip ansi and carriage returns for the browser MVP
		const text = stripAnsi(decoder.decode(data, { stream: true })).replace(/\r/g, "");

		// Keep rolling buffer
		outputChunks.push(text);
		outputBytes += Buffer.byteLength(text, "utf-8");
		while (outputBytes > maxOutputBytes && outputChunks.length > 1) {
			const removed = outputChunks.shift()!;
			outputBytes -= Buffer.byteLength(removed, "utf-8");
		}

		// Stream to callback
		if (options?.onChunk) {
			options.onChunk(text);
		}
	};

	try {
		const result = await operations.exec(command, cwd, {
			onData,
			signal: options?.signal,
		});

		const fullOutput = outputChunks.join("");
		const cancelled = options?.signal?.aborted ?? false;

		return {
			output: fullOutput,
			exitCode: cancelled ? undefined : (result.exitCode ?? undefined),
			cancelled,
			truncated: false,
		};
	} catch (_err) {
		const fullOutput = outputChunks.join("");
		const cancelled = options?.signal?.aborted ?? false;

		return {
			output: fullOutput,
			exitCode: cancelled ? undefined : 1,
			cancelled,
			truncated: false,
		};
	}
}
