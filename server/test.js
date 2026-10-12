// Smoke test for the game server: a host and a guest pair up and relay.
//   node server/test.js
'use strict';
const { spawn } = require('child_process');
const path = require('path');

const PORT = 18787;
const net = require('net');
const srv = spawn(process.execPath, [path.join(__dirname, 'server.js')],
                  { env: { ...process.env, PORT: String(PORT), HEARTBEAT_MS: '200', TIMEOUT_MS: '700' } });

// A raw client that joins a room and then never answers again.
function silentJoin(code) {
  const sock = net.connect(PORT, 'localhost', () => {
    sock.write('GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
               'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n');
    const text = Buffer.from(`JOIN|${code}|1.1.0`);
    const mask = Buffer.from([1, 2, 3, 4]);
    const body = Buffer.from(text.map((b, i) => b ^ mask[i & 3]));
    sock.write(Buffer.concat([Buffer.from([0x81, 0x80 | text.length]), mask, body]));
  });
  sock.on('data', () => {}); // read, but never reply (no pongs)
  return sock;
}

function client() {
  const ws = new WebSocket(`ws://localhost:${PORT}`);
  const inbox = [];
  const waiters = [];
  ws.onmessage = (e) => {
    const w = waiters.shift();
    if (w) w(e.data); else inbox.push(e.data);
  };
  return {
    open: () => new Promise((r) => { ws.onopen = r; }),
    send: (m) => ws.send(m),
    next: () => new Promise((r) => (inbox.length ? r(inbox.shift()) : waiters.push(r))),
    close: () => ws.close(),
  };
}

function expect(got, want) {
  if (!(want instanceof RegExp ? want.test(got) : got === want))
    throw new Error(`expected ${want}, got ${got}`);
  console.log('  ok', got);
}

(async () => {
  await new Promise((r) => setTimeout(r, 400));
  const host = client(), guest = client(), stranger = client();
  await Promise.all([host.open(), guest.open(), stranger.open()]);

  host.send('HOST|1.1.0');
  const room = await host.next();
  expect(room, /^ROOM\|[A-Z]{4}$/);
  const code = room.split('|')[1];

  stranger.send('JOIN|ZZZZ|1.1.0');
  expect(await stranger.next(), 'ERR|No battle with that code');
  stranger.send('JOIN|' + code + '|0.9');
  expect(await stranger.next(), /^ERR\|Different game versions/);

  guest.send('JOIN|' + code.toLowerCase() + '|1.1.0');
  expect(await guest.next(), 'JOINED|' + code);
  expect(await host.next(), 'PEER|joined');

  host.send('RELAY|SETUP|0|42');
  expect(await guest.next(), 'MSG|SETUP|0|42');
  guest.send('RELAY|IN|2');
  expect(await host.next(), 'MSG|IN|2');
  guest.send('PING');
  expect(await guest.next(), 'PONG');

  stranger.send('JOIN|' + code + '|1.1.0');
  expect(await stranger.next(), 'ERR|That battle is already full');

  guest.close();
  expect(await host.next(), 'PEER|left');

  // A guest that goes silent is dropped by the heartbeat.
  host.send('HOST|1.1.0');
  const room2 = (await host.next()).split('|')[1];
  const silent = silentJoin(room2);
  expect(await host.next(), 'PEER|joined');
  expect(await host.next(), 'PEER|left');
  silent.destroy();

  console.log('SERVER TEST PASSED');
  srv.kill();
  process.exit(0);
})().catch((e) => { console.error('SERVER TEST FAILED:', e.message); srv.kill(); process.exit(1); });
