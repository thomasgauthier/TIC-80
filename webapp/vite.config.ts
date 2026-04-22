import { defineConfig } from "vite";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = fileURLToPath(new URL(".", import.meta.url));
const repoDir = path.resolve(rootDir, "..");

export default defineConfig({
  server: {
    fs: {
      allow: [repoDir],
    },
  },
  preview: {
    host: "0.0.0.0",
  },
});
