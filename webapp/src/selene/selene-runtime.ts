import { createLinter, type SeleneDiagnostic } from "../vendor/selene-wasm/selene.js";

export type SeleneNotifyLevel = "info" | "warning" | "error";

const SELENE_TOML = `std = "lua51+tic80"

[lints]
unscoped_variables = "allow"
undefined_variable = "allow"
unused_variable = "allow"
multiple_statements = "allow"
`;

const TIC80_STD_YAML = `---
name: tic80
globals:
  TIC:
    property: full-write
  BOOT:
    property: full-write
  SCN:
    property: full-write
  BDR:
    property: full-write
  MENU:
    property: full-write

  print:
    args:
      - required: false
        type: "..."
  cls:
    args:
      - required: false
        type: number
  pix:
    args:
      - type: number
      - type: number
      - required: false
        type: number
  line:
    args:
      - type: number
      - type: number
      - type: number
      - type: number
      - required: false
        type: number
  rect:
    args:
      - type: number
      - type: number
      - type: number
      - type: number
      - required: false
        type: number
  rectb:
    args:
      - type: number
      - type: number
      - type: number
      - type: number
      - required: false
        type: number
  spr:
    args:
      - type: number
      - type: number
      - type: number
      - required: false
        type: "..."
  map:
    args:
      - required: false
        type: "..."
  mget:
    args:
      - type: number
      - type: number
  mset:
    args:
      - type: number
      - type: number
      - type: number
  circ:
    args:
      - type: number
      - type: number
      - type: number
      - required: false
        type: number
  circb:
    args:
      - type: number
      - type: number
      - type: number
      - required: false
        type: number
  elli:
    args:
      - required: false
        type: "..."
  trib:
    args:
      - required: false
        type: "..."
  tri:
    args:
      - required: false
        type: "..."
  clip:
    args:
      - required: false
        type: "..."
  font:
    args:
      - required: false
        type: "..."
  ttri:
    args:
      - required: false
        type: "..."
  textri:
    args:
      - required: false
        type: "..."
  paint:
    args:
      - required: false
        type: "..."

  btn:
    args:
      - required: false
        type: number
      - required: false
        type: number
  btnp:
    args:
      - required: false
        type: number
      - required: false
        type: number
      - required: false
        type: number
  key:
    args:
      - required: false
        type: number
  keyp:
    args:
      - required: false
        type: number
      - required: false
        type: number
      - required: false
        type: number
  mouse:
    args: []

  sfx:
    args:
      - required: false
        type: "..."
  music:
    args:
      - required: false
        type: "..."

  peek:
    args:
      - type: number
  poke:
    args:
      - type: number
      - type: number
  peek1:
    args:
      - type: number
  poke1:
    args:
      - type: number
      - type: number
  peek2:
    args:
      - type: number
  poke2:
    args:
      - type: number
      - type: number
  peek4:
    args:
      - type: number
  poke4:
    args:
      - type: number
      - type: number
  memcpy:
    args:
      - type: number
      - type: number
      - type: number
  memset:
    args:
      - type: number
      - type: number
      - type: number
  pmem:
    args:
      - type: number
      - required: false
        type: number

  trace:
    args:
      - required: false
        type: any
  time:
    args: []
  tstamp:
    args: []
  exit:
    args: []
  reset:
    args: []
  sync:
    args:
      - required: false
        type: "..."
  vbank:
    args:
      - required: false
        type: number

  fget:
    args:
      - type: number
      - required: false
        type: number
  fset:
    args:
      - type: number
      - type: number
      - required: false
        type: bool

  fft:
    args:
      - type: number
  ffts:
    args:
      - type: number

  map_width__:
    property: read-only
  map_height__:
    property: read-only
  spritesize__:
    property: read-only
  print__:
    property: read-only
  trace__:
    property: read-only
  spr__:
    property: read-only
  mgeti__:
    property: read-only
`;

let linterPromise: Promise<ReturnType<typeof createLinter>> | null = null;

async function getLinter() {
	if (!linterPromise) {
		linterPromise = Promise.resolve(createLinter(SELENE_TOML, TIC80_STD_YAML));
	}
	return await linterPromise;
}

export async function lintLuaText(source: string): Promise<SeleneDiagnostic[]> {
	const linter = await getLinter();
	return await linter.lint(source);
}

export function sortDiagnostics(diagnostics: SeleneDiagnostic[]): SeleneDiagnostic[] {
	return [...diagnostics].sort((a, b) => a.line - b.line || a.column - b.column || a.message.localeCompare(b.message));
}

export function formatDiagnostic(filePath: string, diagnostic: SeleneDiagnostic): string {
	const location = `${filePath}:${diagnostic.line}:${diagnostic.column}`;
	const suffix = diagnostic.code ? ` [${diagnostic.code}]` : "";
	return `${location}: ${diagnostic.message}${suffix}`;
}

export function formatDiagnostics(filePath: string, diagnostics: SeleneDiagnostic[]): string {
	return sortDiagnostics(diagnostics).map((diagnostic) => formatDiagnostic(filePath, diagnostic)).join("\n");
}

export function getNotifyLevel(diagnostics: SeleneDiagnostic[]): SeleneNotifyLevel {
	if (diagnostics.some((diagnostic) => diagnostic.severity === "Error")) {
		return "error";
	}
	if (diagnostics.some((diagnostic) => diagnostic.severity === "Warning" || diagnostic.severity === "Unknown")) {
		return "warning";
	}
	return "info";
}

export function hasErrors(diagnostics: SeleneDiagnostic[]): boolean {
	return diagnostics.some((diagnostic) => diagnostic.severity === "Error");
}
