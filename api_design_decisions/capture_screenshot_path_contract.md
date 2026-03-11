## `capture_screenshot(path)` Uses TIC-FS-Relative Paths

Decision:

- The `path` argument for the MCP `capture_screenshot` tool is a path inside the active TIC-80 filesystem root.
- It is not a general host-filesystem path write primitive.

## Contract

- If `path` is omitted, the tool writes `mcp_capture.png`.
- If `path` is provided, it must be interpreted relative to the active TIC filesystem root.
- Absolute host paths like `/workspace/.../shot.png` are not part of the contract.
- Paths that attempt to escape the TIC filesystem root should also be rejected.

## Why This Design

- The implementation already saves through TIC-80 filesystem APIs rather than raw host-path writes.
- Keeping the tool TIC-FS-relative preserves a clean boundary between TIC-80 behavior and host-environment behavior.
- This is simpler for agents to reason about once documented clearly:
  - set the TIC filesystem root appropriately
  - then save screenshots using relative paths inside that root
- Avoiding arbitrary host-path writes also keeps the tool surface narrower and safer.

## Consequence For MCP Behavior

The bug is not that screenshot capture fails in general. The bug is that the API currently accepts arbitrary-looking `path` strings without making the real contract explicit.

So the expected behavior should be:

- relative TIC-FS path: accepted
- omitted path: accepted
- absolute host path: rejected with a clear structured error

Recommended error text:

`path must be relative to the TIC filesystem root`

## Example

Valid:

- `capture_screenshot({})`
- `capture_screenshot({"path":"shots/intro.png"})`
- `capture_screenshot({"path":"state/run_frame"})`

Invalid:

- `capture_screenshot({"path":"/workspace/tic80/testing_it_playground/.tmp/tic80-run-screen.png"})`
- `capture_screenshot({"path":"../../outside.png"})`
