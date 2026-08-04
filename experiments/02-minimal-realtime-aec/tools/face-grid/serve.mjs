import {createReadStream, existsSync, statSync} from 'node:fs';
import {createServer} from 'node:http';
import {extname, join, normalize} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('./dist/', import.meta.url));
const requestedPort = Number(process.env.PORT ?? 4173);
const mime = new Map([
  ['.css', 'text/css; charset=utf-8'],
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.jsonl', 'application/x-ndjson; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'],
  ['.wasm', 'application/wasm'],
  ['.wav', 'audio/wav'],
]);

const server = createServer((request, response) => {
  const pathname = decodeURIComponent(
    new URL(request.url ?? '/', 'http://127.0.0.1').pathname,
  );
  const relative = pathname === '/' ? 'index.html' : pathname.slice(1);
  const path = normalize(join(root, relative));
  if (!path.startsWith(root) || !existsSync(path) || !statSync(path).isFile()) {
    response.writeHead(404, {'content-type': 'text/plain; charset=utf-8'});
    response.end('Not found');
    return;
  }
  response.writeHead(200, {
    'cache-control': 'no-store',
    'cross-origin-opener-policy': 'same-origin',
    'cross-origin-embedder-policy': 'require-corp',
    'content-type': mime.get(extname(path)) ?? 'application/octet-stream',
  });
  createReadStream(path).pipe(response);
});

server.listen(requestedPort, '127.0.0.1', () => {
  const address = server.address();
  if (typeof address !== 'object' || address === null) {
    throw new Error('Unable to resolve server address');
  }
  process.stdout.write(
    `${JSON.stringify({url: `http://127.0.0.1:${address.port}`})}\n`,
  );
});
