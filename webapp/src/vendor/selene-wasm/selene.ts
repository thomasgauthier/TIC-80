import init, { lint_lua_with_config } from './pkg/selene_wasm.js';

export interface SeleneDiagnostic {
    code: string;
    message: string;
    line: number;
    column: number;
    severity: 'Error' | 'Warning' | 'Allow' | 'Unknown';
}

export class SeleneLinter {
    private tomlConfig: string;
    private stdYaml?: string;
    private initialized: boolean = false;

    constructor(tomlConfig: string, stdYaml?: string) {
        this.tomlConfig = tomlConfig;
        this.stdYaml = stdYaml;
    }

    /**
     * Ensures the underlying WebAssembly module is initialized.
     * This is called automatically by `lint`, but can be called manually to pre-load.
     */
    async initialize() {
        if (!this.initialized) {
            await init();
            this.initialized = true;
        }
    }

    /**
     * Lints the provided Lua code using the configuration provided at creation.
     * @param luaCode The raw Lua source code to lint.
     */
    async lint(luaCode: string): Promise<SeleneDiagnostic[]> {
        await this.initialize();
        return lint_lua_with_config(luaCode, this.tomlConfig, this.stdYaml) as SeleneDiagnostic[];
    }
}

/**
 * Factory to create a new Selene linter instance.
 * @param tomlConfig A string containing the selene.toml configuration.
 * @param stdYaml Optional: A string containing a custom YAML standard library definition.
 */
export function createLinter(tomlConfig: string, stdYaml?: string): SeleneLinter {
    return new SeleneLinter(tomlConfig, stdYaml);
}
