import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const webappDir = path.resolve(scriptDir, "..");
const distDir = path.join(webappDir, "dist");

const requiredFiles = [
  "index.html",
  "tic80-runtime/index.html",
  "tic80-runtime/tic80.js",
  "tic80-runtime/tic80.wasm",
  "tic80-runtime/tic80ctl-browser-core.js",
  "tic80-runtime/tic80ctl-browser-core.wasm",
  "tic80-runtime/serviceworker.js",
];

for (const relativePath of requiredFiles) {
  const absolutePath = path.join(distDir, relativePath);
  await fs.access(absolutePath);
}

const html = await fs.readFile(path.join(distDir, "index.html"), "utf8");
for (const snippet of ["Start owned iframe session", "Start owned popup session", "Pi browser TUI"]) {
  if (!html.includes(snippet)) {
    throw new Error(`Missing expected HTML snippet: ${snippet}`);
  }
}

console.log("dist verification passed");
