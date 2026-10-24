const http = require('http');
const fs = require('fs').promises;
const path = require('path');

// This file is used for testing wasm build from emscripten
// Example build command:
// emcmake cmake -B build-wasm -DGGML_WEBGPU=ON -DLLAMA_OPENSSL=OFF
// cmake --build build-wasm --target test-backend-ops -j

const PORT = 8080;
const STATIC_DIR = path.resolve(__dirname, '../build-wasm/bin');
console.log(`Serving static files from: ${STATIC_DIR}`);

const mimeTypes = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.json': 'application/json',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
};

async function generateDirListing(dirPath, reqUrl) {
  const files = await fs.readdir(dirPath);
  let html = `
    <!DOCTYPE html>
    <html>
    <head>
      <title>Directory Listing</title>
      <style>
        body { font-family: Arial, sans-serif; padding: 20px; }
        ul { list-style: none; padding: 0; }
        li { margin: 5px 0; }
        a { text-decoration: none; color: #0066cc; }
        a:hover { text-decoration: underline; }
      </style>
    </head>
    <body>
      <h1>Directory: ${reqUrl}</h1>
      <ul>
  `;

  if (reqUrl !== '/') {
    html += `<li><a href="../">../ (Parent Directory)</a></li>`;
  }

  for (const file of files) {
    const filePath = path.resolve(dirPath, file);
    const relativePath = path.relative(dirPath, filePath);
    if (
      relativePath.startsWith('..' + path.sep) ||
      relativePath === '..' ||
      path.isAbsolute(relativePath)
    ) {
      continue;
    }
    const stats = await fs.stat(filePath);
    const link = encodeURIComponent(file) + (stats.isDirectory() ? '/' : '');
    html += `<li><a href="${link}">${file}${stats.isDirectory() ? '/' : ''}</a></li>`;
  }

  html += `
      </ul>
    </body>
    </html>
  `;
  return html;
}

const server = http.createServer(async (req, res) => {
  try {
    // Set COOP and COEP headers
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    res.setHeader('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    res.setHeader('Pragma', 'no-cache');
    res.setHeader('Expires', '0');

    // Parse the request target as a URL path. The base is only a parser
    // anchor; query strings are not part of the file path.
    let decodedPath;
    try {
      decodedPath = decodeURIComponent(new URL(req.url, 'http://127.0.0.1').pathname);
    } catch {
      res.writeHeader(400, { 'Content-Type': 'text/plain' });
      res.end('400 Bad Request');
      return;
    }

    if (decodedPath.includes('\0')) {
      res.writeHeader(400, { 'Content-Type': 'text/plain' });
      res.end('400 Bad Request');
      return;
    }

    // Keep the URL path relative to the static root. A leading "." stops a
    // leading slash from selecting a filesystem root or another drive.
    const normalized = path.normalize('.' + path.sep + decodedPath);
    const filePath = path.resolve(STATIC_DIR, normalized);
    const relativePath = path.relative(STATIC_DIR, filePath);
    if (
      relativePath.startsWith('..' + path.sep) ||
      relativePath === '..' ||
      path.isAbsolute(relativePath)
    ) {
      res.writeHeader(400, { 'Content-Type': 'text/plain' });
      res.end('400 Bad Request');
      return;
    }

    const stats = await fs.stat(filePath);

    if (stats.isDirectory()) {
      const indexPath = path.join(filePath, 'index.html');
      try {
        const indexData = await fs.readFile(indexPath);
        res.writeHeader(200, { 'Content-Type': 'text/html' });
        res.end(indexData);
      } catch {
        // No index.html, generate directory listing
        const dirListing = await generateDirListing(filePath, decodedPath);
        res.writeHeader(200, { 'Content-Type': 'text/html' });
        res.end(dirListing);
      }
    } else {
      const ext = path.extname(filePath).toLowerCase();
      const contentType = mimeTypes[ext] || 'application/octet-stream';
      const data = await fs.readFile(filePath);
      res.writeHeader(200, { 'Content-Type': contentType });
      res.end(data);
    }
  } catch (err) {
    if (err.code === 'ENOENT') {
      res.writeHeader(404, { 'Content-Type': 'text/plain' });
      res.end('404 Not Found');
    } else {
      res.writeHeader(500, { 'Content-Type': 'text/plain' });
      res.end('500 Internal Server Error');
    }
  }
});

server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}/`);
});
