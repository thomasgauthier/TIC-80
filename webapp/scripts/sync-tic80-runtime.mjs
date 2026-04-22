import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const webappDir = path.resolve(scriptDir, "..");
const repoDir = path.resolve(webappDir, "..");
const destinationDir = path.join(webappDir, "public", "tic80-runtime");
const appRuntimeIndexPath = path.join(webappDir, "runtime", "index.html");

const candidateDirs = [
  process.env.TIC80_RUNTIME_DIR,
  path.join(repoDir, "build-web-tic80ctl", "bin"),
  path.join(repoDir, "build", "webapp"),
].filter(Boolean);

const requiredFiles = [
  "tic80.js",
  "tic80.wasm",
  "tic80ctl-browser-core.js",
  "tic80ctl-browser-core.wasm",
];

const optionalFiles = [
  "serviceworker.js",
  "tic80.webmanifest",
  "tic80-180.png",
  "tic80-192.png",
  "tic80-512.png",
];

async function exists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

async function findRuntimeSource() {
  for (const candidateDir of candidateDirs) {
    const hasAllFiles = await Promise.all(
      requiredFiles.map((file) => exists(path.join(candidateDir, file))),
    );
    if (hasAllFiles.every(Boolean)) {
      return candidateDir;
    }
  }

  const searched = candidateDirs.map((dir) => `- ${dir}`).join("\n");
  throw new Error(
    `Could not find a TIC-80 browser runtime with required assets. Searched:\n${searched}\n\n` +
      "Build the TIC-80 web artifacts first or set TIC80_RUNTIME_DIR to a directory containing the required files.",
  );
}

async function findFirstExistingFile(fileName) {
  for (const candidateDir of candidateDirs) {
    const candidatePath = path.join(candidateDir, fileName);
    if (await exists(candidatePath)) {
      return candidatePath;
    }
  }
  return null;
}

async function copyFiles(sourceDir) {
  await fs.mkdir(destinationDir, { recursive: true });

  for (const file of requiredFiles) {
    await fs.copyFile(path.join(sourceDir, file), path.join(destinationDir, file));
  }

  for (const file of optionalFiles) {
    const sourcePath = await findFirstExistingFile(file);
    if (!sourcePath) {
      continue;
    }
    await fs.copyFile(sourcePath, path.join(destinationDir, file));
  }

  await fs.copyFile(appRuntimeIndexPath, path.join(destinationDir, "index.html"));
}

const sourceDir = await findRuntimeSource();
await copyFiles(sourceDir);
console.log(`Synced TIC-80 runtime assets from ${sourceDir} to ${destinationDir}`);
