# TIC-80 MCP Stdio Feature Specification

## Goal
Implement an MCP server mode inside TIC-80 that communicates over stdio using JSON-RPC 2.0, starting with one tool:

- `run_command(str)`: runs a TIC-80 console command and returns its output/result.

## Scope
- In scope:
  - `--mcp` startup mode for headless/stdin-stdout operation.
  - MCP handshake and request/response flow over newline-delimited JSON.
  - Tool discovery and tool execution for `run_command(str)`.
  - Strong stdout isolation so protocol frames are never polluted.
- Out of scope (for this feature):
  - Multiple tools beyond `run_command`.
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
### Tool Name
- `run_command`

### Input Schema
- Object with:
  - `command` (string, required)

### Behavior
- Executes the command exactly as if entered in the TIC-80 fantasy editor console command line.
- Returns command output text in MCP content format.
- If execution fails, returns `isError: true` with useful error text.

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

## PASS Criteria (Feature Completion)
Feature is **PASSED** only when all items below are true:

1. Protocol handshake passes:
   - `initialize` request gets valid response with capabilities.
   - `notifications/initialized` is accepted.
2. Tool discovery passes:
   - `tools/list` includes `run_command` with correct schema.
3. Tool execution passes:
   - `tools/call` for `run_command` executes a valid TIC-80 command and returns output.
   - Invalid command returns structured error (`isError: true`).
   - Console parity holds: commands available in the fantasy editor console are callable via MCP with equivalent behavior.
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
- Add/extend a test covering `run_command` success and failure cases.
- Provide one recorded transcript (request/response) proving:
  - init -> list -> call flow,
  - stdout purity,
  - stderr separation.

## Suggested Milestones
1. Wire `run_command` into `tools/list`.
2. Implement `tools/call` dispatcher for `run_command`.
3. Capture/return TIC-80 console output deterministically.
4. Final stdout audit in `--mcp` mode.
5. Complete evidence checklist and mark feature PASSED.
