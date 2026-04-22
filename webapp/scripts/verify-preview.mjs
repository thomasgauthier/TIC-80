const baseUrl = process.argv[2] || "http://127.0.0.1:4173";

async function expectOk(pathname) {
  const response = await fetch(new URL(pathname, baseUrl));
  if (!response.ok) {
    throw new Error(`Expected ${pathname} to return 2xx, got ${response.status}`);
  }
  return response;
}

const rootHtml = await (await expectOk("/")).text();
for (const snippet of ["Start owned iframe session", "Start owned popup session", "Pi browser TUI"]) {
  if (!rootHtml.includes(snippet)) {
    throw new Error(`Preview root HTML is missing ${snippet}`);
  }
}

await expectOk("/tic80-runtime/index.html");
await expectOk("/tic80-runtime/tic80ctl-browser-core.js");
await expectOk("/tic80-runtime/serviceworker.js");

console.log(`preview verification passed for ${baseUrl}`);
