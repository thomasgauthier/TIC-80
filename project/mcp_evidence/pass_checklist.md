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

## 3. Tool execution
- Evidence: `project/mcp_evidence/transcript_out.jsonl`
- Success: `tools/call` with `run_command` and `help commands` returns `isError:false` and textual command output.
- Failure: `tools/call` with invalid command returns `isError:true` and `unknown command` text.
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
