import path from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig, type Plugin } from "vite";

const rootDir = fileURLToPath(new URL(".", import.meta.url));
const repoDir = path.resolve(rootDir, "..");
const piMonoDir = path.resolve(rootDir, "../../..", "pi-mono");
const codingAgentSrc = path.resolve(piMonoDir, "packages/coding-agent/src");
const browserSrc = path.resolve(rootDir, "src");
const polyfillsDir = path.resolve(browserSrc, "polyfills");
const shimsDir = path.resolve(browserSrc, "shims");

function sourceAlias(relativePath: string, replacement: string) {
  const pattern = relativePath.replace(/[.*+?^${}()|[\]\\]/g, "\\$&").replace(/\.ts$/, "\\.(?:ts|js)");
  return [{ find: new RegExp(`(^|/)${pattern}$`), replacement }];
}

function codingAgentShims(): Plugin {
  const replacements = new Map<string, string>([
    ["config.js", path.resolve(browserSrc, "browser-config.ts")],
    ["photon.js", path.resolve(shimsDir, "browser-photon.ts")],
    ["tools-manager.js", path.resolve(shimsDir, "browser-tools-manager.ts")],
    ["changelog.js", path.resolve(shimsDir, "browser-changelog.ts")],
    ["footer-data-provider.js", path.resolve(shimsDir, "browser-footer-data-provider.ts")],
    ["clipboard.js", path.resolve(shimsDir, "browser-clipboard.ts")],
    ["clipboard-image.js", path.resolve(shimsDir, "browser-clipboard-image.ts")],
    ["package-manager.js", path.resolve(shimsDir, "browser-package-manager.ts")],
    ["extensions/index.js", path.resolve(shimsDir, "browser-extensions.ts")],
    ["prompt-templates.js", path.resolve(shimsDir, "browser-prompt-templates.ts")],
    ["export-html/index.js", path.resolve(shimsDir, "browser-export-html.ts")],
    ["tools/index.js", path.resolve(shimsDir, "browser-tools-index.ts")],
    ["tools/bash.js", path.resolve(shimsDir, "browser-tools-bash.ts")],
    ["bash-executor.js", path.resolve(shimsDir, "browser-bash-executor.ts")],
    ["theme/theme.js", path.resolve(shimsDir, "browser-theme.ts")],
    ["session-selector.js", path.resolve(shimsDir, "browser-session-selector.ts")],
    ["extension-editor.js", path.resolve(shimsDir, "browser-extension-editor.ts")],
    ["login-dialog.js", path.resolve(shimsDir, "browser-login-dialog.ts")],
    ["earendil-announcement.js", path.resolve(shimsDir, "browser-earendil-announcement.ts")],
  ]);

  return {
    name: "coding-agent-browser-shims",
    enforce: "pre",
    resolveId(source, importer) {
      if (!importer || !importer.includes("/packages/coding-agent/src/")) {
        return null;
      }

      for (const [suffix, replacement] of replacements) {
        if (source.endsWith(suffix)) {
          return replacement;
        }
      }

      return null;
    },
  };
}

export default defineConfig({
  plugins: [codingAgentShims()],
  resolve: {
    alias: [
      { find: "node:zlib", replacement: path.resolve(shimsDir, "browser-zlib.ts") },
      { find: "zlib", replacement: path.resolve(shimsDir, "browser-zlib.ts") },
      { find: "hosted-git-info", replacement: path.resolve(shimsDir, "browser-hosted-git-info.ts") },
      { find: "proper-lockfile", replacement: path.resolve(shimsDir, "browser-proper-lockfile.ts") },
      { find: "./config.js", replacement: path.resolve(browserSrc, "browser-config.ts") },
      { find: "../config.js", replacement: path.resolve(browserSrc, "browser-config.ts") },
      { find: "../../config.js", replacement: path.resolve(browserSrc, "browser-config.ts") },
      { find: "vite-plugin-node-polyfills/shims/buffer", replacement: path.resolve(polyfillsDir, "buffer.ts") },
      { find: "vite-plugin-node-polyfills/shims/process", replacement: path.resolve(polyfillsDir, "process.ts") },
      { find: "vite-plugin-node-polyfills/shims/global", replacement: path.resolve(shimsDir, "browser-global.ts") },
      { find: "@mariozechner/pi-ai/oauth", replacement: path.resolve(piMonoDir, "packages/ai/src/oauth.ts") },
      { find: "@mariozechner/pi-ai", replacement: path.resolve(piMonoDir, "packages/ai/src/index.ts") },
      { find: "@mariozechner/pi-agent-core", replacement: path.resolve(piMonoDir, "packages/agent/src/index.ts") },
      { find: "@mariozechner/pi-tui/browser", replacement: path.resolve(piMonoDir, "packages/tui/src/browser.ts") },
      { find: "@mariozechner/pi-tui", replacement: path.resolve(shimsDir, "tui-compat.ts") },
      ...sourceAlias("config.ts", path.resolve(browserSrc, "browser-config.ts")),
      ...sourceAlias("utils/photon.ts", path.resolve(shimsDir, "browser-photon.ts")),
      ...sourceAlias("utils/tools-manager.ts", path.resolve(shimsDir, "browser-tools-manager.ts")),
      ...sourceAlias("utils/changelog.ts", path.resolve(shimsDir, "browser-changelog.ts")),
      ...sourceAlias("core/footer-data-provider.ts", path.resolve(shimsDir, "browser-footer-data-provider.ts")),
      ...sourceAlias("utils/clipboard.ts", path.resolve(shimsDir, "browser-clipboard.ts")),
      ...sourceAlias("utils/clipboard-image.ts", path.resolve(shimsDir, "browser-clipboard-image.ts")),
      ...sourceAlias("core/package-manager.ts", path.resolve(shimsDir, "browser-package-manager.ts")),
      ...sourceAlias("core/extensions/index.ts", path.resolve(shimsDir, "browser-extensions.ts")),
      ...sourceAlias("core/prompt-templates.ts", path.resolve(shimsDir, "browser-prompt-templates.ts")),
      ...sourceAlias("core/export-html/index.ts", path.resolve(shimsDir, "browser-export-html.ts")),
      ...sourceAlias("core/tools/index.ts", path.resolve(shimsDir, "browser-tools-index.ts")),
      ...sourceAlias("core/tools/bash.ts", path.resolve(shimsDir, "browser-tools-bash.ts")),
      ...sourceAlias("core/bash-executor.ts", path.resolve(shimsDir, "browser-bash-executor.ts")),
      ...sourceAlias("modes/interactive/theme/theme.ts", path.resolve(shimsDir, "browser-theme.ts")),
      ...sourceAlias("modes/interactive/components/session-selector.ts", path.resolve(shimsDir, "browser-session-selector.ts")),
      ...sourceAlias("modes/interactive/components/extension-editor.ts", path.resolve(shimsDir, "browser-extension-editor.ts")),
      ...sourceAlias("modes/interactive/components/login-dialog.ts", path.resolve(shimsDir, "browser-login-dialog.ts")),
      ...sourceAlias("modes/interactive/components/earendil-announcement.ts", path.resolve(shimsDir, "browser-earendil-announcement.ts")),
      { find: "buffer", replacement: path.resolve(polyfillsDir, "buffer.ts") },
      { find: "node:buffer", replacement: path.resolve(polyfillsDir, "buffer.ts") },
      { find: "process", replacement: path.resolve(polyfillsDir, "process.ts") },
      { find: "node:process", replacement: path.resolve(polyfillsDir, "process.ts") },
      { find: "events", replacement: path.resolve(polyfillsDir, "events.ts") },
      { find: "node:events", replacement: path.resolve(polyfillsDir, "events.ts") },
      { find: "module", replacement: path.resolve(polyfillsDir, "module.ts") },
      { find: "node:module", replacement: path.resolve(polyfillsDir, "module.ts") },
      { find: "url", replacement: path.resolve(polyfillsDir, "url.ts") },
      { find: "node:url", replacement: path.resolve(polyfillsDir, "url.ts") },
      { find: "stream/promises", replacement: path.resolve(polyfillsDir, "stream-promises.ts") },
      { find: "node:stream/promises", replacement: path.resolve(polyfillsDir, "stream-promises.ts") },
      { find: "fs/promises", replacement: path.resolve(polyfillsDir, "fs-promises.ts") },
      { find: "node:fs/promises", replacement: path.resolve(polyfillsDir, "fs-promises.ts") },
      { find: "fs", replacement: path.resolve(polyfillsDir, "fs.ts") },
      { find: "node:fs", replacement: path.resolve(polyfillsDir, "fs.ts") },
      { find: "path", replacement: path.resolve(polyfillsDir, "path.ts") },
      { find: "node:path", replacement: path.resolve(polyfillsDir, "path.ts") },
      { find: "os", replacement: path.resolve(polyfillsDir, "os.ts") },
      { find: "node:os", replacement: path.resolve(polyfillsDir, "os.ts") },
      { find: "crypto", replacement: path.resolve(polyfillsDir, "crypto.ts") },
      { find: "node:crypto", replacement: path.resolve(polyfillsDir, "crypto.ts") },
      { find: "child_process", replacement: path.resolve(polyfillsDir, "child_process.ts") },
      { find: "node:child_process", replacement: path.resolve(polyfillsDir, "child_process.ts") },
      { find: "stream", replacement: path.resolve(polyfillsDir, "stream.ts") },
      { find: "node:stream", replacement: path.resolve(polyfillsDir, "stream.ts") },
      { find: "readline", replacement: path.resolve(polyfillsDir, "readline.ts") },
      { find: "node:readline", replacement: path.resolve(polyfillsDir, "readline.ts") },
      { find: "string_decoder", replacement: path.resolve(polyfillsDir, "string_decoder.ts") },
      { find: "node:string_decoder", replacement: path.resolve(polyfillsDir, "string_decoder.ts") },
    ],
  },
  define: {
    global: "globalThis",
    "process.env.PI_OFFLINE": JSON.stringify("1"),
  },
  server: {
    fs: {
      allow: [repoDir, piMonoDir],
    },
    host: "0.0.0.0",
  },
  preview: {
    host: "0.0.0.0",
  },
  optimizeDeps: {
    esbuildOptions: {
      sourcemap: true,
    },
  },
  build: {
    sourcemap: true,
  },
});
