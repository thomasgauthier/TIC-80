# MCP Editor Authoring PRD

## Status

First draft.

## Summary

Add a new MCP editor-authoring surface for TIC-80 so an agent can read and mutate the same content that a human edits in the native tools:

- SFX editor data
- music editor data
- sprite and palette data
- map data

This is an **MCP-first, shim-second** feature.

That means:

1. implement the semantic editor tools in MCP first
2. validate those tools directly over MCP
3. only afterward expose Unix-style `tic80ctl` affordances that translate packed CLI syntax into the MCP calls

The point is not to invent a separate authoring model for `tic80ctl`.
The point is to give the agent a reliable, structured way to manipulate real TIC-80 editor state while preserving the existing architectural rule that MCP is the semantic source of truth.

## Problem

The current MCP surface is strong for:

- running console commands
- taking screenshots
- running playtest episodes

It is weak for direct authoring of cartridge content through the major creative editors.

Today, an agent that wants to create or revise audio, music, sprites, palettes, or maps has poor options:

- drive the UI indirectly
- issue ad hoc console commands and memory pokes
- rely on external file transformations rather than editor-native semantics

Those approaches are fragile for agent use because they:

- do not match the editor affordances a human uses
- make read/modify/write flows hard to reason about
- force the agent into low-level storage details too early
- increase token cost and orchestration complexity
- make validation and round-tripping awkward

## Product Goal

Enable an agent to manipulate TIC-80 editor content through narrow MCP tools that map closely to the human editor contract while remaining efficient and reliable for LLM use.

This feature should let an agent do things like:

- set an SFX wavetable and related envelopes
- read back an SFX slot to confirm the edit
- configure music track settings
- assign patterns to frames
- mutate only selected rows in a pattern
- write a sprite by ID or by region
- update a palette
- fill a map rectangle and then place local detail chunks

without forcing the agent to simulate clicks or reason about raw memory layout as the primary interface.

## Design Principles

### 1. MCP First

MCP owns the semantics.

The later `tic80ctl` layer may expose Unix-style subcommands such as:

```sh
tic80ctl sfx set_wavetable ...
tic80ctl music set_frame ...
```

but that is stage 2.

The CLI shim should adapt to MCP, not the other way around.

### 2. Mirror Human Editor Contracts

The public surface should be grounded in what the end-user sees and edits in the TIC-80 UI.

Examples:

- SFX is not just “bytes”; it is wavetable + envelopes + speed + loop points
- music is track settings + frame arrangement + sparse pattern row data
- sprites are 8x8 pixel tiles and palette entries
- maps are a large tile grid edited through broad fills and local detail placement

### 3. Prefer Structured Inputs At MCP Layer

The MCP layer should accept structured JSON inputs, not packed shell-friendly tokens.

Examples:

- MCP should take pattern rows as structured fields
- MCP should take palette entries as structured color values
- MCP should take sprite row data as explicit 2D-friendly data

The later CLI shim may pack these into compact textual forms for Unix invocation.

### 4. LLM Alignment Matters

The surface should be optimized for:

- patterns the model has likely seen in pretraining
- low token overhead
- reliable parsing and round-tripping

This argues for:

- narrow tools
- sparse mutation where data is large and mostly empty
- structured getter output
- avoiding raw packed blobs as the only MCP representation

### 5. Preserve Existing MCP Conventions Where Reasonable

Setter tools should behave like existing MCP tools:

- `isError:false` on success
- short human-readable success text
- `isError:true` on domain-level failures
- JSON-RPC invalid params for malformed schema/type inputs

Getter tools need richer output than the current text-only tools, so they should return proper structured data in the MCP result while still remaining MCP-native.

## Audience

Primary audience:

- a coding agent authoring or revising TIC-80 cartridges

Secondary audience:

- a human developer using the agent and later inspecting or refining the result in the TIC-80 editor

This is not primarily a human hand-authored CLI design.
It is an agent-facing authoring API that must remain legible against the native TIC-80 editor model.

## Goals

- Add MCP tools for direct editor authoring across SFX, music, sprite/palette, and map.
- Make read/modify/write loops practical by adding matching getters.
- Keep the public tool set narrow, explicit, and domain-scoped.
- Preserve sparse mutation semantics where that matches the editor workflow.
- Make validation failures deterministic and atomic.
- Keep future `tic80ctl` exposure straightforward by ensuring each MCP tool maps cleanly to a later CLI command.

## Non-Goals For V1

- Implement the `tic80ctl` shim layer for these editor tools
- invent a single generic “editor utility” dispatcher tool
- replace existing MCP tools or change their semantics
- expose raw memory pokes as the preferred public authoring interface
- add partial-write or best-effort mutation semantics
- cover every possible editor behavior beyond the core editing contract

## End-User Editor Contract

This section captures the human-facing problem space the agent surface should mirror.

### SFX Editor

The SFX editor exposes these core authoring concepts:

- source wavetable
- volume envelope
- wave envelope
- panning envelope
- arpeggio envelope
- pitch envelope
- speed
- loop points

The agent mental model should match the same pipeline:

1. choose the source waveform
2. modulate waveform and pitch over time
3. shape loudness
4. distribute output spatially
5. control duration/repetition with speed and loop points

### Music Editor

The music editor has two distinct scopes:

- macro arrangement
- micro pattern editing

Macro:

- track settings
- frame arrangement
- per-frame per-channel pattern assignment

Micro:

- sparse row edits inside patterns
- note data
- SFX/instrument linkage
- tracker effect command fields and parameters
- note-off / cut semantics

### Sprite Editor

The sprite editor exposes:

- 8x8 sprite tiles
- spritesheet-style addressing by bank/region
- palette-controlled indexed color data

The agent should be able to target either:

- a single sprite by flat ID
- a region by bank/coordinates

### Palette

Palette authoring is logically adjacent to sprite authoring and should allow direct indexed color edits in a representation better aligned to agent reasoning than one continuous packed blob.

### Map Editor

The map editor has two natural editing modes:

- broad geometry edits
- local detail placement

That naturally suggests:

- rectangle-style fills
- chunk-style local writes

rather than a giant full-map replacement interface.

## Proposed MCP Surface

Use the naming convention:

```text
<domain>_<verb>_<object>
```

Examples:

- `sfx_set_wavetable`
- `music_set_frame`
- `sprite_get_sprite`
- `map_set_rect`

### SFX Tools

Setters:

- `sfx_set_wavetable`
- `sfx_set_volume_envelope`
- `sfx_set_pitch_envelope`
- `sfx_set_panning`
- `sfx_set_wave_envelope`
- `sfx_set_arpeggio`
- `sfx_set_speed`
- `sfx_set_loop_points`

Getters:

- `sfx_get_wavetable`
- `sfx_get_volume_envelope`
- `sfx_get_pitch_envelope`
- `sfx_get_panning`
- `sfx_get_wave_envelope`
- `sfx_get_arpeggio`
- `sfx_get_speed`
- `sfx_get_loop_points`

### Music Tools

Setters:

- `music_set_track`
- `music_set_frame`
- `music_set_pattern_row`
- `music_set_pattern_rows`

Getters:

- `music_get_track`
- `music_get_frame`
- `music_get_pattern_row`
- `music_get_pattern_rows`

Notes:

- MCP inputs should use structured fields for row edits
- packed tracker tokens are a later shim concern
- row edits are sparse by default

### Sprite And Palette Tools

Setters:

- `sprite_set_sprite`
- `sprite_set_spritesheet_region`
- `sprite_set_palette`

Getters:

- `sprite_get_sprite`
- `sprite_get_spritesheet_region`
- `sprite_get_palette`

Notes:

- support both sprite addressing modes in v1
- `sprite_set_sprite` / `sprite_get_sprite` are single 8x8 tile operations by flat sprite ID
- `sprite_set_spritesheet_region` / `sprite_get_spritesheet_region` are arbitrary rectangular region operations by `bank`, `x`, `y`, `width`, and `height`
- rectangular region payloads should use a row-major `sprites` array where each entry carries one tile's 8x8 row data
- default sprite content format should stay 2D-friendly at MCP layer
- palette data should be structured around individual colors rather than one mandatory packed string

### Map Tools

Setters:

- `map_set_rect`
- `map_set_chunk`

Getters:

- `map_get_rect`
- `map_get_chunk`

Notes:

- no explicit point tool in v1
- `chunk` is sufficient for local detail writes

## Request Model

### Setter Inputs

All setters should accept structured JSON inputs only.

Examples of design direction:

- pattern rows as explicit fields such as row, note, sfx, command, params
- palette colors as explicit entries
- sprite pixel payloads in row-oriented structured form
- spritesheet region writes as explicit rectangular payloads rather than repeated single-tile calls

The exact schema can be finalized during implementation, but V1 should avoid requiring agents to supply shell-oriented packed strings at MCP level.

### Getter Inputs

Getters should accept whatever minimal targeting information is required to identify the unit or region being read:

- SFX slot
- track/frame/pattern identifiers
- sprite ID or region coordinates
- rectangular sprite regions should include `width` and `height` as part of the selector, not just `bank/x/y`
- map region bounds

### Sparse Mutation

Sparse mutation is the default where it matches the human workflow.

This is required for:

- music pattern edits
- map local writes

It is also appropriate anywhere a full replacement would be unnaturally large or destructive.

## Result Model

### Setter Results

Setter results should remain close to current MCP style.

Success:

- `isError:false`
- short human-readable summary text

Failure:

- `isError:true` for domain/editor failures
- actionable error text

Malformed schema or types:

- JSON-RPC invalid params

### Getter Results

Getter results should return:

- `isError:false`
- proper structured result data

The structured data should be first-class MCP result content, not serialized JSON tunneled through `content[0].text`.

Optional short text summaries are acceptable, but structured data is the primary contract.

## Validation And Error Semantics

All setter tools should be atomic per call.

That means:

- validate the request first
- if any required part is invalid, do not mutate anything
- do not partially apply multi-field or multi-row edits

Error classes:

1. malformed request shape
   - JSON-RPC invalid params
2. valid shape but invalid domain values
   - MCP tool result with `isError:true`
3. internal failure
   - MCP tool result with `isError:true`

Examples of invalid domain values:

- out-of-range SFX index
- invalid note syntax
- unsupported effect code
- palette index out of range
- sprite coordinates outside the valid region
- map rectangle outside valid map bounds

## Symmetric Read/Write Contract

V1 should include matching getters for each setter capability.

Why:

- agents need reliable round-trip validation
- write-only tools make iterative editing brittle
- direct getters reduce the need to infer success through screenshots or unrelated runtime behavior

This symmetry does not require identical payload shapes between setters and getters, but it does require that each setter target can be read back through a dedicated MCP path.

## Why Not A Single Generic Utility Tool

A single MCP utility tool with subcommands is intentionally not the v1 design.

Reasons:

- narrow tool names are easier to discover through `tools/list`
- domain-specific tools keep schemas smaller and validation clearer
- it is easier to preserve editor semantics without inventing an extra dispatch layer
- the later `tic80ctl` shim already provides a natural place for command-oriented UX

## Why Structured MCP But Packed CLI Later

This split is intentional.

MCP side:

- structured JSON is better for validation
- easier to keep semantics explicit
- easier to support structured getter output

CLI shim side:

- packed tokens are better for Unix invocation
- compact textual forms are fine once the shim can translate them
- shell ergonomics should not distort the MCP layer

This is the key architectural boundary for the feature.

## Testing Strategy

### MCP Discovery

- `tools/list` includes every new tool
- schemas are present and coherent

### Setter/Getter Round Trips

For every new setter capability:

1. set valid data
2. read it back through the matching getter
3. assert the expected structured result

### Atomic Failure Coverage

- invalid requests do not partially mutate state
- multi-row and multi-field operations fail as a whole

### Sparse Mutation Coverage

- pattern row writes do not disturb unrelated rows
- map chunk writes do not disturb unrelated cells
- frame edits do not disturb unrelated frames

### Regression Safety

Existing MCP features must continue to work:

- `run_command`
- `capture_screenshot`
- `run_playtest_episode`

Existing protocol expectations must remain true:

- stdout reserved for MCP JSON-RPC frames
- headless-safe MCP execution still valid

### Likely Smoke Additions

- direct MCP smoke for one SFX edit and readback
- direct MCP smoke for one track/frame/pattern edit and readback
- direct MCP smoke for one sprite + palette edit and readback
- direct MCP smoke for one map rectangle + chunk edit and readback

## Tradeoffs

### Structured MCP vs Packed MCP

Structured MCP is less compact than a tightly packed custom string format, but it is more robust, easier to validate, and a better foundation for symmetric getters.

### Many Narrow Tools vs One Generic Tool

Many narrow tools increase tool count, but they preserve domain clarity and reduce per-tool ambiguity.

### Symmetric Getters In V1 vs Setters Only

Adding getters increases initial scope, but it materially improves agent reliability and reduces guesswork during authoring loops.

### Sparse Mutation vs Full Replacement

Sparse mutation is more complex to implement than full replacement, but it matches how music patterns and maps are actually edited and avoids destructive large-payload writes.

## Out Of Scope For This PRD

- exact `tic80ctl` subcommand syntax
- packed CLI token grammar
- shell quoting rules for the later shim
- non-MCP transports
- generalized editor automation beyond the covered domains

Those belong to the later shim PRD or follow-up design notes after MCP behavior is stable.

## Implemented Stage 2 Shim Mapping

The current `tic80ctl` shim exposes the editor MCP surface through domain groups:

- `tic80ctl sfx ...`
- `tic80ctl music ...`
- `tic80ctl sprite ...`
- `tic80ctl map ...`

The shim keeps MCP as the semantic source of truth and uses merged getter/setter behavior by arity:

- selector-only invocation performs the matching getter
- selector plus compact payload performs the matching setter
- `--args-json '<raw MCP arguments>'` is a setter escape hatch that bypasses compact payload parsing

Compact CLI payloads intentionally stay Unix-friendly while mapping one-for-one onto the MCP request model:

- SFX wavetable: `hex32`
- SFX volume, wave, pitch: `tick:value,...`
- SFX arpeggio: CSV semitone list
- SFX panning: `left,right`
- SFX speed: integer
- SFX loop: `start:size`
- music track: `tempo,speed,rows`
- music frame: `p0,p1,p2,p3`
- music row: `note:sfx:cmd`
- music rows setter: `row:note:sfx:cmd,...`
- sprite tile: comma-separated 8x8 hex rows
- sprite region: semicolon-separated row-major tile payloads
- sprite palette: comma-separated `RRGGBB` colors
- map rect setter: single fill tile id
- map chunk setter/getter: row-major CSV tile ids

Getter behavior remains MCP-native:

- `--json` returns the wrapped MCP result with `structuredContent`
- plain output prints the getter's structured content directly for quick inspection

## Bottom Line

The feature is a new MCP-native authoring API for TIC-80 editor content.

It should:

- map to the real editor contracts humans use
- stay structured and explicit at the MCP layer
- support symmetric reads and writes
- preserve existing MCP semantics where possible
- prepare cleanly for a later `tic80ctl` shim without letting shell concerns pollute the MCP design

If this lands correctly, the agent will be able to author and inspect core cartridge content directly through MCP instead of relying on brittle UI-driving or low-level memory tricks.
