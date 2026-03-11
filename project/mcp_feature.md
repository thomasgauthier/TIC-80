# TIC-80 MCP Stdio Feature Specification

## Goal
Implement an MCP server mode inside TIC-80 that communicates over stdio using JSON-RPC 2.0 with two tools:

- `run_command(str)`: runs a TIC-80 console command and returns its output/result.
- `capture_screenshot(path?)`: captures the current live framebuffer and saves it as a PNG file.

## Scope
- In scope:
  - `--mcp` startup mode for headless/stdin-stdout operation.
  - MCP handshake and request/response flow over newline-delimited JSON.
  - Tool discovery and tool execution for `run_command(str)` and `capture_screenshot(path?)`.
  - MCP idle tick progression so wall-clock waits advance run-mode frames.
  - Strong stdout isolation so protocol frames are never polluted.
- Out of scope (for this feature):
  - Non-stdio transports.
  - Rich session state/history APIs beyond command execution.

## Protocol Requirements
- Transport:
  - Input: newline-delimited JSON-RPC messages from `stdin`.
  - Output: newline-delimited JSON-RPC messages to `stdout`.
- RPC:
  - Must support:
    - `initialize`
    - `notifications/initialized`
    - `tools/list`
    - `tools/call`
  - Must respond with valid JSON-RPC 2.0 envelopes (`jsonrpc: "2.0"`).

## Tool Contract
### Tool Name: `run_command`
- Input schema:
  - `command` (string, required)
- Behavior:
  - Executes the command exactly as if entered in the TIC-80 fantasy editor console command line.
  - Returns command output text in MCP content format.
  - If execution fails, returns `isError: true` with useful error text.
  - If a command-triggered script/runtime error happens synchronously during the `run_command` tool call in `--mcp` mode, the error is returned in MCP output and logged to console history without forcing the visible studio mode away from the currently active view.

### Tool Name: `capture_screenshot`
- Input schema:
  - `path` (string, optional)
- Behavior:
  - Captures the current runtime framebuffer (`tic->product.screen`) and encodes it to PNG.
  - Saves the screenshot through TIC filesystem APIs.
  - If `path` is omitted, defaults to deterministic filename `mcp_capture.png`.
  - If `path` has no `.png` extension, `.png` is appended.
  - Returns MCP text content describing saved relative path and resolved absolute path.
  - Returns `isError: true` with useful text when capture/encode/save fails.

### Security/Execution Constraints
- MCP command execution must use the existing TIC-80 console command system.
- Command parity is required: any command available in the fantasy editor console must be callable via MCP `run_command`.
- MCP must not add extra command capabilities beyond what the console already supports.

## Stdout/Stderr Policy (Critical)
- In `--mcp` mode:
  - `stdout` is reserved exclusively for MCP JSON-RPC responses/notifications.
  - Any non-protocol logs/messages/progress must go to `stderr` or be suppressed.
- Outside `--mcp` mode:
  - Existing behavior remains unchanged unless intentionally improved.

## Headless Runtime Requirements
- Must run without opening UI windows in `--mcp` mode.
- Must operate correctly on headless systems where only stdio is available.
- While waiting for incoming stdin messages, MCP mode must keep advancing `studio_tick` in wall-clock time.

## PASS Criteria (Feature Completion)
Feature is **PASSED** only when all items below are true:

1. Protocol handshake passes:
   - `initialize` request gets valid response with capabilities.
   - `notifications/initialized` is accepted.
2. Tool discovery passes:
   - `tools/list` includes `run_command` and `capture_screenshot` with correct schemas.
3. Tool execution passes:
   - `tools/call` for `run_command` executes a valid TIC-80 command and returns output.
   - Invalid command returns structured error (`isError: true`).
   - MCP-triggered command/runtime errors (for example `eval error("boom")`) return structured error (`isError: true`) without forcing subsequent screenshots away from the previously active view.
   - `tools/call` for `capture_screenshot` saves PNG output and returns saved path text (`isError: false`).
   - `capture_screenshot` without `path` writes deterministic default path `mcp_capture.png`.
   - Console parity holds: commands available in the fantasy editor console are callable via MCP with equivalent behavior.
4. Wall-clock progression passes:
   - `run_command` can enter run mode, then after ~5s idle wait, `capture_screenshot` captures an advanced frame.
   - Spontaneous cart/runtime errors that happen after an MCP tool call has already returned still follow normal TIC-80 behavior and become visible in subsequent screenshots.
4. Stdout purity passes:
   - During MCP session, `stdout` contains only JSON-RPC lines.
   - No banner/debug/progress/human text appears on `stdout`.
5. Stderr separation passes:
   - Non-protocol diagnostics (if any) are emitted to `stderr`, not `stdout`.
6. Headless execution passes:
   - `./build/bin/tic80 --mcp` works end-to-end without GUI requirements.
7. Regression safety passes:
   - Existing non-MCP startup/CLI behavior still works.

## Required Test Evidence
- Automated smoke script succeeds against built binary:
  - `tools/mcp/stdio_smoke.sh ./build/bin/tic80`
- Automated regression script succeeds against built binary:
  - `tools/mcp/error_mode_regression.sh ./build/bin/tic80`
- Test must include `run_command("run")`, a 5-second wait, and `capture_screenshot` with artifact validation.
- Add/extend coverage for `run_command` success/failure and `capture_screenshot` success.
- Add/extend automated coverage for MCP-triggered command errors using a stable Lua text-project fixture with explicit cart data sections.
- Add/extend automated coverage for spontaneous cart/runtime errors using the same stable fixture and verify that a later screenshot changes after the delayed cart error.
- Provide one recorded transcript (request/response) proving:
  - init -> list -> call flow for both tools,
  - stdout purity,
  - stderr separation.

## Suggested Milestones
1. Wire `run_command` and `capture_screenshot` into `tools/list`.
2. Implement `tools/call` dispatcher for both tools with argument validation.
3. Capture/return TIC-80 console output and screenshot paths deterministically.
4. Ensure idle MCP loop advances `studio_tick` with wall-clock waits.
5. Final stdout audit in `--mcp` mode.
6. Complete evidence checklist and mark feature PASSED.
