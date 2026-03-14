/**
 * phoneCtrl – WebRTC Signaling Server
 *
 * Brokers SDP offers/answers and ICE candidates between the laptop host and
 * one or more phone clients so that a direct WebRTC peer connection can be
 * established.
 *
 * Protocol (all messages are JSON strings):
 *
 *   Phone → Server:
 *     { "type": "offer",    "sdp": "<SDP string>" }
 *     { "type": "ice",      "candidate": "…", "sdpMid": "…", "sdpMLineIndex": 0 }
 *     { "type": "register", "role": "phone" }
 *
 *   Laptop → Server:
 *     { "type": "answer",   "sdp": "<SDP string>" }
 *     { "type": "ice",      "candidate": "…", "sdpMid": "…", "sdpMLineIndex": 0 }
 *     { "type": "register", "role": "laptop" }
 *
 * The server forwards offers and ICE candidates from the phone to the laptop
 * and vice-versa.  Only one laptop connection is supported at a time; multiple
 * phones can connect sequentially but each gets its own peer connection.
 *
 * Usage:
 *   node server.js [--port 3000] [--host 0.0.0.0]
 */

'use strict';

const http  = require('http');
const { WebSocketServer, WebSocket } = require('ws');

// ── Parse CLI arguments ───────────────────────────────────────────────────────

function parseArgs() {
  const args = process.argv.slice(2);
  let port = 3000;
  let host = '0.0.0.0';
  for (let i = 0; i < args.length - 1; i++) {
    if (args[i] === '--port') port = parseInt(args[i + 1], 10);
    if (args[i] === '--host') host = args[i + 1];
  }
  return { port, host };
}

// ── State ─────────────────────────────────────────────────────────────────────

/** @type {WebSocket|null} */
let laptopSocket = null;

/**
 * Map from phone client ID to WebSocket.
 * @type {Map<number, WebSocket>}
 */
const phoneClients = new Map();
let nextPhoneId = 1;

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Safely send a JSON object to a WebSocket client.
 * @param {WebSocket} ws
 * @param {object} data
 */
function send(ws, data) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(data));
  }
}

/** Forward a message to all connected phone clients. */
function broadcastToPhones(data) {
  for (const ws of phoneClients.values()) {
    send(ws, data);
  }
}

// ── WebSocket server ──────────────────────────────────────────────────────────

const { port, host } = parseArgs();
const httpServer = http.createServer();
const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (ws, req) => {
  const clientAddr = req.socket.remoteAddress;
  console.log(`[signaling] New connection from ${clientAddr}`);

  let role = null;   // 'laptop' | 'phone'
  let phoneId = null;

  ws.on('message', (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      console.warn('[signaling] Received non-JSON message – ignored');
      return;
    }

    const { type } = msg;

    // ── Registration ─────────────────────────────────────────────────────────
    if (type === 'register') {
      role = msg.role;
      if (role === 'laptop') {
        if (laptopSocket && laptopSocket.readyState === WebSocket.OPEN) {
          send(ws, { type: 'error', message: 'A laptop is already registered.' });
          ws.close();
          return;
        }
        laptopSocket = ws;
        console.log('[signaling] Laptop registered');
        send(ws, { type: 'registered', role: 'laptop' });

      } else if (role === 'phone') {
        phoneId = nextPhoneId++;
        phoneClients.set(phoneId, ws);
        console.log(`[signaling] Phone registered (id=${phoneId})`);
        send(ws, { type: 'registered', role: 'phone', id: phoneId });

        // Notify the laptop that a new phone is ready to negotiate
        send(laptopSocket, { type: 'phone_connected', phoneId });
      }
      return;
    }

    // ── SDP Offer (phone → laptop) ────────────────────────────────────────────
    if (type === 'offer') {
      console.log(`[signaling] Forwarding offer (phone ${phoneId} → laptop)`);
      send(laptopSocket, { ...msg, phoneId });
      return;
    }

    // ── SDP Answer (laptop → phone) ───────────────────────────────────────────
    if (type === 'answer') {
      const targetPhoneId = msg.phoneId;
      const phoneWs = phoneClients.get(targetPhoneId);
      console.log(`[signaling] Forwarding answer (laptop → phone ${targetPhoneId})`);
      send(phoneWs, { type: 'answer', sdp: msg.sdp });
      return;
    }

    // ── ICE Candidate ─────────────────────────────────────────────────────────
    if (type === 'ice') {
      if (role === 'phone') {
        // Phone's ICE candidate → forward to laptop
        send(laptopSocket, { ...msg, phoneId });
      } else if (role === 'laptop') {
        // Laptop's ICE candidate → forward to the specific phone or broadcast
        if (msg.phoneId) {
          const phoneWs = phoneClients.get(msg.phoneId);
          send(phoneWs, { type: 'ice', candidate: msg.candidate,
                          sdpMid: msg.sdpMid, sdpMLineIndex: msg.sdpMLineIndex });
        } else {
          broadcastToPhones({ type: 'ice', candidate: msg.candidate,
                              sdpMid: msg.sdpMid, sdpMLineIndex: msg.sdpMLineIndex });
        }
      }
      return;
    }

    console.warn(`[signaling] Unknown message type: ${type}`);
  });

  ws.on('close', () => {
    if (role === 'laptop') {
      console.log('[signaling] Laptop disconnected');
      laptopSocket = null;
      broadcastToPhones({ type: 'laptop_disconnected' });
    } else if (role === 'phone' && phoneId !== null) {
      console.log(`[signaling] Phone ${phoneId} disconnected`);
      phoneClients.delete(phoneId);
      send(laptopSocket, { type: 'phone_disconnected', phoneId });
    } else {
      console.log(`[signaling] Unregistered client from ${clientAddr} disconnected`);
    }
  });

  ws.on('error', (err) => {
    console.error(`[signaling] WebSocket error: ${err.message}`);
  });
});

httpServer.listen(port, host, () => {
  console.log(`phoneCtrl signaling server listening on ws://${host}:${port}`);
  console.log('Waiting for laptop and phone connections…');
});

// ── Graceful shutdown ─────────────────────────────────────────────────────────
process.on('SIGINT',  () => { console.log('\nShutting down…'); wss.close(); process.exit(0); });
process.on('SIGTERM', () => { wss.close(); process.exit(0); });
