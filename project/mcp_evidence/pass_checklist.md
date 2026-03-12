# MCP Feature PASS Checklist

Source spec: `project/mcp_feature.md`
<!-- Historical note: this checklist contains legacy evidence captured before console-parity spec updates. -->

## 1. Protocol handshake
- Evidence: `project/mcp_evidence/transcript_out.jsonl`
- `initialize` request returns JSON-RPC 2.0 result with capabilities.
- `notifications/initialized` accepted (no error response emitted).

## 2. Tool discovery
- Evidence: `project/mcp_evidence/transcript_out.jsonl`
- `tools/list` includes `run_command` with `command` string schema.
- `tools/list` includes `capture_screenshot` with relative-path screenshot contract.
- `tools/list` includes `run_playtest_episode` with `script`, `timeout_seconds`, and `input_overlay` arguments.

## 3. Tool execution
- Evidence: `project/mcp_evidence/transcript_out.jsonl`
- Success: `tools/call` with `run_command` and `help commands` returns `isError:false` and textual command output.
- Failure: `tools/call` with invalid command returns `isError:true` and `unknown command` text.
- MCP-triggered command/runtime error: `tools/call` with `run_command` and `eval error("mcp eval boom")` returns `isError:true`, and a later screenshot remains on the prior active view.
- Screenshot path contract: `tools/call` with `capture_screenshot` and an absolute host path returns `isError:true` with a relative-path contract error.
- Unsupported: `tools/call` with `run_command` and `folder` returns `isError:true` and an MCP-safe unsupported-command error.
<!-- Historical note: this `folder` expectation reflects an earlier spec revision and is kept for archival context. -->
<!-- Current spec direction is console-command parity via MCP. -->

## 4. Stdout purity
- Evidence: `project/mcp_evidence/transcript_out.jsonl`
- All lines are JSON-RPC frames.

## 5. Stderr separation
- Evidence: `project/mcp_evidence/transcript_err.log`
- File is empty for MCP protocol exchange.

## 6. Headless execution
- Evidence command: `xvfb-run --auto-servernum ./build/bin/tic80 --mcp < project/mcp_evidence/transcript_req.jsonl`
- Result files: `project/mcp_evidence/transcript_out.jsonl`, `project/mcp_evidence/transcript_err.log`

## 7. Automated tests
- Test script: `tools/mcp/stdio_smoke.sh`
- Includes success and failure checks for `run_command`.
- Smoke run log: `project/mcp_evidence/stdio_smoke.log`
- Playtest smoke script: `tools/mcp/playtest_episode_smoke.sh`
- Playtest regression script: `tools/mcp/playtest_episode_regression.sh`
- Regression script: `tools/mcp/error_mode_regression.sh`
- Regression checks:
  - Uses `tools/mcp/fixtures/mcp_error_mode.lua` as a stable startup-loaded Lua fixture with explicit cart data sections.
  - Warms the cart into a stable run frame before comparing screenshots.
  - MCP-triggered `eval` error keeps the captured framebuffer stable across screenshots.
  - A delayed spontaneous runtime error changes the later captured framebuffer, matching normal TIC-80 behavior.
- Playtest episode checks:
  - Uses `tools/mcp/fixtures/playtest_episode.lua` as a deterministic startup-loaded Lua fixture.
  - Verifies `run_playtest_episode` can execute a one-frame script and create `script.lua`, `log.txt`, `console.txt`, and `screenshots/000001.png`.
  - Verifies one-frame injected input changes the captured frame as expected.
  - Verifies overlay-enabled episode screenshots differ from overlay-disabled screenshots.
  - Verifies cart `trace(...)` output is recorded in `console.txt`.
  - Verifies only the latest three `./playtest/episode_n` artifact directories are retained.
