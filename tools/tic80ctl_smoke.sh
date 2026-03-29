#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$1"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SESSION_DIR="$(mktemp -d)"
STATE_DIR="$SESSION_DIR/state"
BAD_STATE_DIR="$SESSION_DIR/bad-state"
EPISODE_SCRIPT="$SESSION_DIR/episode.lua"
TIMEOUT_EPISODE_SCRIPT="$SESSION_DIR/timeout_episode.lua"
LINT_OK_CART="$SESSION_DIR/lint_ok.lua"
LINT_BAD_CART="$SESSION_DIR/lint_bad_palette.lua"
LINT_DUP_CART="$SESSION_DIR/lint_duplicate_palette.lua"
LINT_LONG_CODE_CART="$SESSION_DIR/lint_long_code.lua"
LINT_OK_PLAYTEST="$SESSION_DIR/lint_playtest_ok.lua"
LINT_HEURISTIC_PLAYTEST="$SESSION_DIR/lint_playtest_heuristic.lua"
LINT_BAD_PLAYTEST="$SESSION_DIR/lint_playtest_bad.lua"

cleanup() {
  TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" stop >/dev/null 2>&1 || true
  rm -f "$ROOT/.tmp_tic80ctl_run_ok.lua"
  rm -rf "$SESSION_DIR"
}
trap cleanup EXIT

cat > "$EPISODE_SCRIPT" <<'EOF'
log("smoke")
frameadvance()
end_episode("done", "smoke")
EOF
cat > "$TIMEOUT_EPISODE_SCRIPT" <<'EOF'
while true do
end
EOF
cat > "$LINT_OK_PLAYTEST" <<'EOF'
-- tic80ctl: playtest-script

log("lint")
frameadvance()
end_episode("done", "lint")
EOF
cat > "$LINT_HEURISTIC_PLAYTEST" <<'EOF'
local function hold(input, frames)
  for i=1,frames do
    set_input(input)
    frameadvance()
  end
end

hold({right=true}, 4)
end_episode("done", "heuristic")
EOF
cat > "$LINT_BAD_PLAYTEST" <<'EOF'
set_input({right=true})
log("forgot to advance")
EOF
cat > "$LINT_OK_CART" <<'EOF'
-- title:  lint ok
-- script: lua

function TIC()
end

-- <PALETTE>
-- 000:1a1c2c5d275d0f6f763b5dc970c1f0a7f070f1c05dff8f5dff5d9cc2497f7a30454b1d27fdeeedfff8e68a6f452c5c49
-- </PALETTE>
EOF
cat > "$LINT_BAD_CART" <<'EOF'
-- title:  lint bad
-- script: lua

function TIC()
end

-- <PALETTE>
-- 000:1a1c2c5d275d0f6f766b8f3ea7f070c05dd9a066f1e7a8fff8e6c86b4a7a3045c12c249fdeeed6a1cf6b8f
-- </PALETTE>
EOF
cat > "$LINT_DUP_CART" <<'EOF'
-- title:  lint dup
-- script: lua

function TIC()
end

-- <PALETTE>
-- 000:1a1c2c5d275d0f6f763b5dc970c1f0a7f070f1c05dff8f5dff5d9cc2497f7a30454b1d27fdeeedfff8e68a6f452c5c49
-- </PALETTE>

-- <PALETTE>
-- 001:000000111111222222333333444444555555666666777777888888999999aaaaaabbbbbbccccccddddddeeeeeeffffff
-- </PALETTE>
EOF
head -c 524289 /dev/zero | tr '\0' 'a' > "$LINT_LONG_CODE_CART"
printf '\n' >> "$LINT_LONG_CODE_CART"
cat >> "$LINT_LONG_CODE_CART" <<'EOF'
-- <PALETTE>
-- 000:1a1c2c5d275d0f6f763b5dc970c1f0a7f070f1c05dff8f5dff5d9cc2497f7a30454b1d27fdeeedfff8e68a6f452c5c49
-- </PALETTE>
EOF
EMPTY_CART="$SESSION_DIR/empty.lua"
: > "$EMPTY_CART"
RUN_OK_CART=".tmp_tic80ctl_run_ok.lua"
cat > "$ROOT/$RUN_OK_CART" <<'EOF'
function TIC()
end
EOF

START_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" start)"
printf '%s\n' "$START_OUT" | grep -q '^started tic80ctl session'

HELP_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --help)"
printf '%s\n' "$HELP_OUT" | grep -q '^tic80ctl$'
printf '%s\n' "$HELP_OUT" | grep -q '^quick start:$'
printf '%s\n' "$HELP_OUT" | grep -q '^  tic80ctl eval "trace(type(TIC))"$'

HELP_TOPIC_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" help playtest)"
printf '%s\n' "$HELP_TOPIC_OUT" | grep -q '^tic80ctl playtest --script-file <file>'
printf '%s\n' "$HELP_TOPIC_OUT" | grep -q 'artifacts are written under ./playtest/episode_N/'

HELP_LINT_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" help lint-cart)"
printf '%s\n' "$HELP_LINT_OUT" | grep -q '^tic80ctl lint-cart <file>$'
printf '%s\n' "$HELP_LINT_OUT" | grep -q 'Validate a TIC-80 script cart offline before you try to load it.'

HELP_PLAYTEST_LINT_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" help lint-playtest-script)"
printf '%s\n' "$HELP_PLAYTEST_LINT_OUT" | grep -q '^tic80ctl lint-playtest-script <file>$'
printf '%s\n' "$HELP_PLAYTEST_LINT_OUT" | grep -q 'Validate a Lua playtest episode script offline'

LINT_OK_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-cart "$LINT_OK_CART")"
printf '%s\n' "$LINT_OK_OUT" | grep -q "^lint ok: $LINT_OK_CART$"

LINT_OK_JSON="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json lint-cart "$LINT_OK_CART")"
printf '%s\n' "$LINT_OK_JSON" | jq -e '.ok == true and .kind == "script_cart" and .message == "lint ok"' >/dev/null

PLAYTEST_LINT_OK_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-playtest-script "$LINT_OK_PLAYTEST")"
printf '%s\n' "$PLAYTEST_LINT_OK_OUT" | grep -q "^lint ok: $LINT_OK_PLAYTEST$"

PLAYTEST_LINT_OK_JSON="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json lint-playtest-script "$LINT_OK_PLAYTEST")"
printf '%s\n' "$PLAYTEST_LINT_OK_JSON" | jq -e '.ok == true and .kind == "playtest_script" and .message == "lint ok"' >/dev/null

PLAYTEST_LINT_HEURISTIC_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-playtest-script "$LINT_HEURISTIC_PLAYTEST")"
printf '%s\n' "$PLAYTEST_LINT_HEURISTIC_OUT" | grep -q 'heuristic playtest script'

set +e
LINT_BAD_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-cart "$LINT_BAD_CART" 2>&1)"
LINT_BAD_STATUS=$?
set -e
[ "$LINT_BAD_STATUS" -ne 0 ]
printf '%s\n' "$LINT_BAD_OUT" | grep -q "section <PALETTE> row 000 has 86 hex chars; expected 96"

set +e
LINT_BAD_JSON="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json lint-cart "$LINT_BAD_CART" 2>&1)"
LINT_BAD_JSON_STATUS=$?
set -e
[ "$LINT_BAD_JSON_STATUS" -ne 0 ]
printf '%s\n' "$LINT_BAD_JSON" | grep -q '"ok":false'
printf '%s\n' "$LINT_BAD_JSON" | grep -q '"line":8'

set +e
LINT_DUP_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-cart "$LINT_DUP_CART" 2>&1)"
LINT_DUP_STATUS=$?
set -e
[ "$LINT_DUP_STATUS" -ne 0 ]
printf '%s\n' "$LINT_DUP_OUT" | grep -q 'duplicate section block <PALETTE>; TIC-80 text loader only reads the first block'

set +e
PLAYTEST_LINT_BAD_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-playtest-script "$LINT_BAD_PLAYTEST" 2>&1)"
PLAYTEST_LINT_BAD_STATUS=$?
set -e
[ "$PLAYTEST_LINT_BAD_STATUS" -ne 0 ]
printf '%s\n' "$PLAYTEST_LINT_BAD_OUT" | grep -q 'uses set_input() but never calls frameadvance()'

set +e
PLAYTEST_LINT_CART_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-playtest-script "$LINT_OK_CART" 2>&1)"
PLAYTEST_LINT_CART_STATUS=$?
set -e
[ "$PLAYTEST_LINT_CART_STATUS" -ne 0 ]
printf '%s\n' "$PLAYTEST_LINT_CART_OUT" | grep -q 'looks like a TIC-80 script cart'

set +e
LINT_LONG_CODE_OUT="$(TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" lint-cart "$LINT_LONG_CODE_CART" 2>&1)"
LINT_LONG_CODE_STATUS=$?
set -e
[ "$LINT_LONG_CODE_STATUS" -ne 0 ]
printf '%s\n' "$LINT_LONG_CODE_OUT" | grep -q 'code before first tagged section is 524289 bytes; TIC-80 loader truncates at 524288'

set +e
BAD_START_OUT="$(timeout 5s env TIC80CTL_STATE_DIR="$BAD_STATE_DIR" TIC80CTL_BIN="$SESSION_DIR/missing-tic80-bin" "$ROOT/tic80ctl" start 2>&1)"
BAD_START_STATUS=$?
set -e
[ "$BAD_START_STATUS" -ne 0 ]
[ "$BAD_START_STATUS" -ne 124 ]
printf '%s\n' "$BAD_START_OUT" | grep -q '^tic80ctl:'

STATUS_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" status)"
printf '%s\n' "$STATUS_OUT" | grep -q '^running pid='

set +e
LOAD_FAIL_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load "$EMPTY_CART" 2>&1)"
LOAD_FAIL_STATUS=$?
set -e
[ "$LOAD_FAIL_STATUS" -ne 0 ]
printf '%s\n' "$LOAD_FAIL_OUT" | grep -q '^load failed: project loading error$'
printf '%s\n' "$LOAD_FAIL_OUT" | grep -qF "last load target: $EMPTY_CART"
printf '%s\n' "$LOAD_FAIL_OUT" | grep -q '^stderr tail: >$'

set +e
LOAD_FAIL_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" cmd --json "load $EMPTY_CART" 2>&1)"
LOAD_FAIL_JSON_STATUS=$?
set -e
[ "$LOAD_FAIL_JSON_STATUS" -ne 0 ]
printf '%s\n' "$LOAD_FAIL_JSON" | jq -e '.response.result.structuredContent.command == "load" and .response.result.structuredContent.error_kind == "project_loading_error" and .diagnostics.verb == "load" and .diagnostics.failure_kind == "project_loading_error" and (.diagnostics.last_load_target | endswith("/empty.lua"))' >/dev/null

set +e
EVAL_FAIL_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" eval "trace(type(TIC))" 2>&1)"
EVAL_FAIL_STATUS=$?
set -e
[ "$EVAL_FAIL_STATUS" -ne 0 ]
printf '%s\n' "$EVAL_FAIL_OUT" | grep -q '^eval failed: runtime not initialized$'
printf '%s\n' "$EVAL_FAIL_OUT" | grep -q '^stderr tail: >$'

set +e
EVAL_FAIL_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" cmd --json "eval trace(type(TIC))" 2>&1)"
EVAL_FAIL_JSON_STATUS=$?
set -e
[ "$EVAL_FAIL_JSON_STATUS" -ne 0 ]
printf '%s\n' "$EVAL_FAIL_JSON" | jq -e '.response.result.structuredContent.command == "eval" and .response.result.structuredContent.error_kind == "runtime_not_initialized" and .diagnostics.verb == "eval" and .diagnostics.failure_kind == "runtime_not_initialized" and .diagnostics.hint == "the run command did not start a VM"' >/dev/null

set +e
START_WITH_CART_ERR="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" start tools/mcp/fixtures/playtest_episode.lua 2>&1)"
START_WITH_CART_STATUS=$?
set -e
[ "$START_WITH_CART_STATUS" -ne 0 ]
printf '%s\n' "$START_WITH_CART_ERR" | grep -q '^tic80ctl: start does not accept a cart argument; use `tic80ctl load <cart>` after start$'

CMD_LOAD_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" cmd --json "load tools/mcp/fixtures/playtest_episode.lua")"
printf '%s\n' "$CMD_LOAD_JSON" | jq -e '.response.result.isError == false' >/dev/null

STATUS_AFTER_CMD="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" status)"
printf '%s\n' "$STATUS_AFTER_CMD" | grep -q '^running pid='

LOAD_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load --json tools/mcp/fixtures/playtest_episode.lua)"
printf '%s\n' "$LOAD_JSON" | jq -e '.response.result.isError == false' >/dev/null

RUN_OK_LOAD_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load "$RUN_OK_CART")"
printf '%s\n' "$RUN_OK_LOAD_OUT" | grep -q 'cart '

RUN_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" run)"
printf '%s\n' "$RUN_OUT" | grep -q '^run started$'

LOAD_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load --json tools/mcp/fixtures/playtest_episode.lua)"
printf '%s\n' "$LOAD_JSON" | jq -e '.response.result.isError == false' >/dev/null

RUN_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" run --json)"
printf '%s\n' "$RUN_JSON" | jq -e '.response.result.isError == false and .response.result.structuredContent.command == "run" and .response.result.structuredContent.recognized == true and .response.result.structuredContent.mode_after == "run" and .response.result.structuredContent.core_initialized_after == true and .response.result.structuredContent.error_kind == "none" and .diagnostics.verb == "run" and .diagnostics.last_load_target == "tools/mcp/fixtures/playtest_episode.lua"' >/dev/null

EVAL_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" eval "trace(type(TIC))")"
printf '%s\n' "$EVAL_OUT" | grep -q 'function'

SCREEN_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" screenshot tic80ctl_capture.png)"
printf '%s\n' "$SCREEN_OUT" | grep -q 'saved screenshot: tic80ctl_capture.png'
[ -s "$ROOT/tic80ctl_capture.png" ]

PLAYTEST_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" playtest --script-file "$EPISODE_SCRIPT")"
printf '%s\n' "$PLAYTEST_OUT" | grep -q 'status=done'
printf '%s\n' "$PLAYTEST_OUT" | grep -q 'artifact_path=./playtest/episode_'

set +e
PLAYTEST_TIMEOUT_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" playtest --script-file "$TIMEOUT_EPISODE_SCRIPT" --timeout 11 2>&1)"
PLAYTEST_TIMEOUT_STATUS=$?
set -e
[ "$PLAYTEST_TIMEOUT_STATUS" -ne 0 ]
printf '%s\n' "$PLAYTEST_TIMEOUT_OUT" | grep -q '^status=timeout$'
printf '%s\n' "$PLAYTEST_TIMEOUT_OUT" | grep -q '^message=playtest episode timed out$'
if printf '%s\n' "$PLAYTEST_TIMEOUT_OUT" | grep -q 'timed out waiting for MCP response'; then
  exit 1
fi

SFX_SET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" sfx wavetable 0 0123456789abcdef0123456789abcdef)"
printf '%s\n' "$SFX_SET_OUT" | grep -q '^updated sfx wavetable$'

SFX_GET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" sfx wavetable 0)"
printf '%s\n' "$SFX_GET_OUT" | grep -q '^sfx=0$'
printf '%s\n' "$SFX_GET_OUT" | grep -q '^values=\[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\]$'

MUSIC_FRAME_SET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" music frame --args-json '{"track":0,"frame":0,"patterns":[1,2,3,4]}')"
printf '%s\n' "$MUSIC_FRAME_SET_OUT" | grep -q '^updated frame$'

MUSIC_FRAME_GET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" music frame 0 0)"
printf '%s\n' "$MUSIC_FRAME_GET_OUT" | grep -q '^track=0$'
printf '%s\n' "$MUSIC_FRAME_GET_OUT" | grep -q '^patterns=\[1,2,3,4\]$'

SPRITE_SET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" sprite tile 3 01234567,89abcdef,00112233,44556677,8899aabb,ccddeeff,13579bdf,2468ace0)"
printf '%s\n' "$SPRITE_SET_OUT" | grep -q '^updated sprite$'

SPRITE_GET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" sprite tile 3)"
printf '%s\n' "$SPRITE_GET_OUT" | grep -q '^id=3$'
printf '%s\n' "$SPRITE_GET_OUT" | grep -q '^rows=\["01234567","89abcdef","00112233","44556677","8899aabb","ccddeeff","13579bdf","2468ace0"\]$'

MAP_SET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" map chunk 10 12 3 2 1,2,3,4,5,6)"
printf '%s\n' "$MAP_SET_OUT" | grep -q '^updated map chunk$'

MAP_GET_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" map chunk 10 12 3 2)"
printf '%s\n' "$MAP_GET_OUT" | grep -q '^tiles=\[1,2,3,4,5,6\]$'

set +e
SCREEN_ERR="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" screenshot shots/tic80ctl_capture.png 2>&1)"
SCREEN_ERR_STATUS=$?
set -e
[ "$SCREEN_ERR_STATUS" -ne 0 ]
printf '%s\n' "$SCREEN_ERR" | grep -q 'relative screenshot directory does not exist: shots'

SFX_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sfx wavetable 2 0123456789abcdef0123456789abcdef)"
printf '%s\n' "$SFX_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

SFX_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sfx wavetable 2)"
printf '%s\n' "$SFX_GET_JSON" | jq -e '.response.result.structuredContent.values[15] == 15' >/dev/null

MUSIC_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json music frame 0 0 1,2,3,4)"
printf '%s\n' "$MUSIC_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

MUSIC_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json music frame 0 0)"
printf '%s\n' "$MUSIC_GET_JSON" | jq -e '.response.result.structuredContent.patterns == [1,2,3,4]' >/dev/null

SPRITE_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite tile 3 01234567,89abcdef,01234567,89abcdef,01234567,89abcdef,01234567,89abcdef)"
printf '%s\n' "$SPRITE_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

SPRITE_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite tile 3)"
printf '%s\n' "$SPRITE_GET_JSON" | jq -e '.response.result.structuredContent.rows[0] == "01234567"' >/dev/null

SPRITE_ARGS_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite tile --args-json '{"id":5,"rows":["11111111","11111111","11111111","11111111","11111111","11111111","11111111","11111111"]}' 5)"
printf '%s\n' "$SPRITE_ARGS_JSON" | jq -e '.response.result.isError == false' >/dev/null

SPRITE_ARGS_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite tile 5)"
printf '%s\n' "$SPRITE_ARGS_GET_JSON" | jq -e '.response.result.structuredContent.rows[0] == "11111111"' >/dev/null

PALETTE_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite palette ff0000,ee1100,dd2200,cc3300,bb4400,aa5500,996600,887700,778800,669900,55aa00,44bb00,33cc00,22dd00,11ee00,00ff00)"
printf '%s\n' "$PALETTE_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

PALETTE_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json sprite palette)"
printf '%s\n' "$PALETTE_GET_JSON" | jq -e '.response.result.structuredContent.colors[0] == "ff0000"' >/dev/null

MAP_RECT_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json map rect 1 2 2 2 9)"
printf '%s\n' "$MAP_RECT_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

MAP_RECT_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json map rect 1 2 2 2)"
printf '%s\n' "$MAP_RECT_GET_JSON" | jq -e '.response.result.structuredContent.tiles == [9,9,9,9]' >/dev/null

MAP_CHUNK_SET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json map chunk 4 5 3 2 1,2,3,4,5,6)"
printf '%s\n' "$MAP_CHUNK_SET_JSON" | jq -e '.response.result.isError == false' >/dev/null

MAP_CHUNK_GET_JSON="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" --json map chunk 4 5 3 2)"
printf '%s\n' "$MAP_CHUNK_GET_JSON" | jq -e '.response.result.structuredContent.tiles == [1,2,3,4,5,6]' >/dev/null

for attempt in 1 2 3; do
  TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" stop >/dev/null 2>&1 || true

  RESTART_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" start)"
  printf '%s\n' "$RESTART_OUT" | grep -q '^started tic80ctl session'

  RELOAD_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load "$RUN_OK_CART")"
  printf '%s\n' "$RELOAD_OUT" | grep -q 'cart '

  RERUN_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" run)"
  printf '%s\n' "$RERUN_OUT" | grep -q '^run started$'

  REEVAL_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" eval "trace(type(TIC))")"
  printf '%s\n' "$REEVAL_OUT" | grep -q 'function'
done

STOP_OUT="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" stop)"
printf '%s\n' "$STOP_OUT" | grep -q '^stopped tic80ctl session'

STATUS_STOPPED="$(TIC80CTL_STATE_DIR="$STATE_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" status || true)"
printf '%s\n' "$STATUS_STOPPED" | grep -q '^tic80ctl: no active session$'

echo "tic80ctl smoke test passed."
