// Chakravyuha game server: pairs two players by a room code and relays their
// messages. Plain Node.js (no packages): the WebSocket handshake and framing
// are implemented here, which is all a turn-by-turn board game needs.
//
//   node server/server.js            (listens on $PORT, default 8787)
//
// Protocol: one text line per WebSocket message, fields separated by "|".
//   client -> server                 server -> client
//   HOST|<version>                   ROOM|<code>          room created
//   JOIN|<code>|<version>            JOINED|<code>        you are in
//   RELAY|<payload>                  PEER|joined / PEER|left
//   PING                             MSG|<payload>        from the other player
//                                    PONG, ERR|<reason>
// The game itself (who moves where) is decided by the host's copy of the
// game; the server never looks inside RELAY payloads.

'use strict';
const http = require('http');
const crypto = require('crypto');

const PORT = Number(process.env.PORT) || 8787;
const MAX_MESSAGE = 1024;                  // game messages are tiny
const CODE_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ'; // no I or O: easy to read aloud
const rooms = new Map();                   // code -> { host, guest, version, created }

// ---- HTTP: a health check so hosting platforms know the server is up -------
const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
  res.end(`Chakravyuha game server: ${rooms.size} room(s) open\n`);
});

// ---- WebSocket handshake (RFC 6455) --------------------------------------------
server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key'];
  if (!key || (req.headers.upgrade || '').toLowerCase() !== 'websocket') {
    socket.end('HTTP/1.1 400 Bad Request\r\n\r\n');
    return;
  }
  const accept = crypto.createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n' +
               `Connection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  const client = new Client(socket);
  socket.on('data', (chunk) => client.receive(chunk));
  socket.on('close', () => client.closed());
  socket.on('error', () => socket.destroy());
});

class Client {
  constructor(socket) {
    this.socket = socket;
    this.buffer = Buffer.alloc(0);
    this.room = null;
    this.alive = true;
  }

  // Parse complete frames out of the byte stream.
  receive(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (this.buffer.length >= 2) {
      const b0 = this.buffer[0], b1 = this.buffer[1];
      const opcode = b0 & 0x0f, masked = (b1 & 0x80) !== 0;
      let len = b1 & 0x7f, offset = 2;
      if (len === 126) {
        if (this.buffer.length < 4) return;
        len = this.buffer.readUInt16BE(2); offset = 4;
      } else if (len === 127) {
        this.close(); return;               // far larger than any game message
      }
      if (!masked || len > MAX_MESSAGE) { this.close(); return; } // clients must mask
      if (this.buffer.length < offset + 4 + len) return;           // wait for the rest
      const mask = this.buffer.subarray(offset, offset + 4);
      const data = Buffer.from(this.buffer.subarray(offset + 4, offset + 4 + len));
      for (let i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
      this.buffer = this.buffer.subarray(offset + 4 + len);

      if (opcode === 0x1) this.message(data.toString('utf8'));    // text
      else if (opcode === 0x8) { this.close(); return; }          // close
      else if (opcode === 0x9) this.frame(0xA, data);              // ping -> pong
    }
  }

  frame(opcode, payload) {
    if (!this.alive) return;
    const len = payload.length;
    const header = len < 126 ? Buffer.from([0x80 | opcode, len])
                             : Buffer.from([0x80 | opcode, 126, len >> 8, len & 0xff]);
    this.socket.write(Buffer.concat([header, payload]));
  }

  send(line) { this.frame(0x1, Buffer.from(line, 'utf8')); }

  close() {
    if (!this.alive) return;
    this.frame(0x8, Buffer.alloc(0));
    this.alive = false;
    this.socket.end();
    this.closed();
  }

  // ---- Rooms --------------------------------------------------------------------
  message(line) {
    const [type, a, b] = line.split('|');
    const room = this.room && rooms.get(this.room);

    if (type === 'PING') return this.send('PONG');

    if (type === 'HOST') {
      this.leave();
      let code;
      do {
        code = Array.from({ length: 4 },
          () => CODE_LETTERS[crypto.randomInt(CODE_LETTERS.length)]).join('');
      } while (rooms.has(code));
      rooms.set(code, { host: this, guest: null, version: a || '', created: Date.now() });
      this.room = code;
      return this.send(`ROOM|${code}`);
    }

    if (type === 'JOIN') {
      const code = (a || '').toUpperCase().trim();
      const target = rooms.get(code);
      if (!target) return this.send('ERR|No battle with that code');
      if (target.guest) return this.send('ERR|That battle is already full');
      if (target.version && b && target.version !== b)
        return this.send(`ERR|Different game versions (${target.version} vs ${b})`);
      this.leave();
      target.guest = this;
      this.room = code;
      this.send(`JOINED|${code}`);
      return target.host.send('PEER|joined');
    }

    if (type === 'RELAY' && room) {
      const other = room.host === this ? room.guest : room.host;
      if (other) other.send('MSG|' + line.slice(6));
      return;
    }

    if (type === 'LEAVE') return this.leave();
  }

  leave() {
    const room = this.room && rooms.get(this.room);
    if (room) {
      const other = room.host === this ? room.guest : room.host;
      if (other) { other.send('PEER|left'); other.room = null; }
      rooms.delete(this.room);
    }
    this.room = null;
  }

  closed() {
    this.alive = false;
    this.leave();
  }
}

// Forget rooms nobody joined within an hour.
setInterval(() => {
  const cutoff = Date.now() - 60 * 60 * 1000;
  for (const [code, room] of rooms)
    if (!room.guest && room.created < cutoff) { room.host.close(); rooms.delete(code); }
}, 5 * 60 * 1000).unref();

server.listen(PORT, () => console.log(`Chakravyuha game server on port ${PORT}`));
