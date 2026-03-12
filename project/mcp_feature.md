# TIC-80 MCP Stdio Feature Specification

## Goal
Implement an MCP server mode inside TIC-80 that communicates over stdio using JSON-RPC 2.0 with two tools:

- `run_command(str)`: runs a TIC-80 console command and returns its output/result.
- `capture_screenshot(path?)`: captures the current live framebuffer and saves it as a PNG file.

## Scope
- In scope:
  - `--mcp` startup mode for MCP stdio transport.
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
  - If `path` is provided, it must be relative to the active TIC filesystem root.
  - If `path` has no `.png` extension, `.png` is appended.
  - Absolute host paths and escaping paths are rejected with `isError: true`.
  - Returns MCP text content describing saved relative path and resolved absolute path.
  - Returns `isError: true` with useful text when capture/encode/save fails.

### Tool Name: `run_playtest_episode`
- Input schema:
  - `script` (string, required)
  - `timeout_seconds` (integer, optional)
  - `input_overlay` (boolean, optional)
- Behavior:
  - Runs a constrained playtest Lua script in a separate episode-owned Lua runtime.
  - Supports `frameadvance()`, `set_input(...)`, `log(...)`, and `end_episode(...)`.
  - Advances gameplay deterministically one frame at a time through the normal run-mode tick path.
  - Writes artifacts under `./playtest/episode_n/`, including:
    - `script.lua`
    - `log.txt`
    - `console.txt`
    - `screenshots/000001.png`, `000002.png`, ...
  - Captures one screenshot per advanced frame.
  - When `input_overlay` is true, draws frame/input overlay into the saved screenshot artifact without changing normal screenshot behavior elsewhere.
  - Retains only the latest three episode directories.
  - Returns MCP text content summarizing status, message, frame count, and artifact path.
  - Returns `isError: true` with useful text when script execution or artifact generation fails.

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
- `--mcp` is orthogonal to graphical vs headless execution.
- `--mcp` must work in normal graphical environments.
- Must operate correctly on headless systems where only stdio is available.
- Headless execution may be achieved externally, for example with `xvfb-run --soft`.
- While waiting for incoming stdin messages, MCP mode must keep advancing `studio_tick` in wall-clock time.

## PASS Criteria (Feature Completion)
Feature is **PASSED** only when all items below are true:

1. Protocol handshake passes:
   - `initialize` request gets valid response with capabilities.
   - `notifications/initialized` is accepted.
2. Tool discovery passes:
   - `tools/list` includes `run_command`, `capture_screenshot`, and `run_playtest_episode` with correct schemas.
3. Tool execution passes:
   - `tools/call` for `run_command` executes a valid TIC-80 command and returns output.
   - Invalid command returns structured error (`isError: true`).
   - MCP-triggered command/runtime errors (for example `eval error("boom")`) return structured error (`isError: true`) without forcing subsequent screenshots away from the previously active view.
   - `tools/call` for `capture_screenshot` saves PNG output and returns saved path text (`isError: false`).
   - `capture_screenshot` without `path` writes deterministic default path `mcp_capture.png`.
   - `capture_screenshot` rejects absolute or escaping paths with structured error (`isError: true`).
   - `tools/call` for `run_playtest_episode` executes a one-frame playtest script and returns `isError: false` with status, message, frame count, and artifact path text.
   - `run_playtest_episode` writes `script.lua`, `log.txt`, `console.txt`, and per-frame screenshots under `./playtest/episode_n/`.
   - `run_playtest_episode` supports one-frame input injection for player 1 by default.
   - `run_playtest_episode` can save screenshots with and without overlay and produces differing artifact PNGs when overlay is enabled.
   - `run_playtest_episode` retains only the latest three episode directories.
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
   - `./build/bin/tic80 --mcp` works end-to-end in graphical environments.
   - `xvfb-run --auto-servernum ./bin/tic80 --skip --soft --mcp` works end-to-end for headless validation.
7. Regression safety passes:
   - Existing non-MCP startup/CLI behavior still works.

## Required Test Evidence
- Automated smoke script succeeds against built binary:
  - `tools/mcp/stdio_smoke.sh ./build/bin/tic80`
- Automated playtest smoke script succeeds against built binary:
  - `tools/mcp/playtest_episode_smoke.sh ./build/bin/tic80`
- Automated regression script succeeds against built binary:
  - `tools/mcp/error_mode_regression.sh ./build/bin/tic80`
- Automated playtest regression script succeeds against built binary:
  - `tools/mcp/playtest_episode_regression.sh ./build/bin/tic80`
- Test must include `run_command("run")`, a 5-second wait, and `capture_screenshot` with artifact validation.
- Add/extend coverage for `run_command` success/failure and `capture_screenshot` success.
- Add/extend automated coverage for MCP-triggered command errors using a stable Lua text-project fixture with explicit cart data sections.
- Add/extend automated coverage for spontaneous cart/runtime errors using the same stable fixture and verify that a later screenshot changes after the delayed cart error.
- Add automated coverage for `run_playtest_episode` using a small deterministic Lua fixture that proves:
  - one-frame script execution
  - per-frame screenshot artifact creation
  - one-frame input injection
  - overlay-on vs overlay-off artifact difference
  - `trace(...)` capture into `console.txt`
  - rolling retention of the latest three episode artifacts
- Provide one recorded transcript (request/response) proving:
  - init -> list -> call flow for both tools,
  - stdout purity,
  - stderr separation.
- Headless validation may rely on external environment setup rather than an in-process no-window mode.

## Suggested Milestones
1. Wire `run_command` and `capture_screenshot` into `tools/list`.
2. Implement `tools/call` dispatcher for both tools with argument validation.
3. Capture/return TIC-80 console output and screenshot paths deterministically.
4. Ensure idle MCP loop advances `studio_tick` with wall-clock waits.
5. Final stdout audit in `--mcp` mode.
6. Complete evidence checklist and mark feature PASSED.
