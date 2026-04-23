# Selene browser fixtures

These `.lua` files are intended to exercise the browser-bundled Selene extension in `./webapp/`.

Suggested manual checks in Pi (`#pi-terminal`):

- `write` or `edit` one of these into `/workspace/<name>.lua`
- confirm the `[selene]` extension output is appended
- confirm the notification level matches expectation

Fixture overview:

- `01_clean_tic.lua` — clean TIC-80 script; expected clean
- `02_undefined_global.lua` — references unknown global; with current bundled config, likely still clean because `undefined_variable = "allow"`
- `03_unused_local.lua` — unused local; with current bundled config, likely still clean because `unused_variable = "allow"`
- `04_tic80_globals.lua` — uses TIC-80 globals like `cls`, `spr`, `trace`; expected clean if `lua51+tic80` stdlib wiring works
- `05_syntax_error.lua` — invalid Lua syntax; expected Selene/WASM parse failure or lint error
- `06_plain_lua.lua` — plain Lua file; expected clean
- `07_warning_candidate.lua` — a possible non-fatal warning candidate depending on Selene behavior/config
