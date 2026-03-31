const { WebSocketServer } = require('ws');
const net = require('net');
const fs = require('fs');
const path = require('path');

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
const NODES = (
  process.env.NODES ||
  'node-a:localhost:8001,node-b:localhost:8002,node-c:localhost:8003,node-d:localhost:8004,node-e:localhost:8005'
)
  .split(',')
  .map((entry) => {
    const [id, host, port] = entry.split(':');
    return { id, host, port: parseInt(port, 10) };
  });

const POLL_INTERVAL = parseInt(process.env.POLL_INTERVAL || '500', 10);
const WS_PORT = parseInt(process.env.WS_PORT || '3001', 10);
const DATA_DIR =
  process.env.DATA_DIR || path.join(__dirname, '..', 'data');

// ---------------------------------------------------------------------------
// Node-to-ticker mapping
// ---------------------------------------------------------------------------
const NODE_TICKERS = {
  'node-a': 'AAPL',
  'node-b': 'MSFT',
  'node-c': 'NVDA',
  'node-d': 'AMZN',
  'node-e': 'GOOG',
};

// ---------------------------------------------------------------------------
// TCP helpers  (the C++ server closes the connection after each response)
// ---------------------------------------------------------------------------
function sendTcp(host, port, command, timeout = 2000) {
  return new Promise((resolve, reject) => {
    const client = new net.Socket();
    let data = '';
    let done = false;

    const timer = setTimeout(() => {
      if (!done) {
        done = true;
        client.destroy();
        resolve(data.trim() || null);
      }
    }, timeout);

    client.connect(port, host, () => {
      client.write(command + '\n');
    });

    client.on('data', (chunk) => {
      data += chunk.toString();
    });

    client.on('end', () => {
      if (!done) {
        done = true;
        clearTimeout(timer);
        resolve(data.trim());
      }
    });

    client.on('error', (err) => {
      if (!done) {
        done = true;
        clearTimeout(timer);
        reject(err);
      }
    });
  });
}

// ---------------------------------------------------------------------------
// Response parsers
// ---------------------------------------------------------------------------
function parseBook(raw) {
  const lines = raw.split('\n');
  const bids = [];
  const asks = [];
  let section = null;

  for (const line of lines) {
    const t = line.trim();
    if (t === 'BIDS') { section = 'bids'; continue; }
    if (t === 'ASKS') { section = 'asks'; continue; }
    if (!t) continue;
    const parts = t.split(/\s+/);
    if (parts.length >= 2) {
      const price = parseFloat(parts[0]);
      const quantity = parseInt(parts[1], 10);
      if (!isNaN(price) && !isNaN(quantity)) {
        (section === 'bids' ? bids : asks).push({ price, quantity });
      }
    }
  }
  return { bids, asks };
}

function parseStatus(raw) {
  const status = { node_id: '', orders: 0, op_log: 0, clock: {} };
  for (const line of raw.split('\n')) {
    const t = line.trim();
    if (t.startsWith('NODE ')) status.node_id = t.slice(5);
    else if (t.startsWith('ORDERS ')) status.orders = parseInt(t.slice(7), 10);
    else if (t.startsWith('OP_LOG ')) status.op_log = parseInt(t.slice(7), 10);
    else if (t.startsWith('CLOCK')) {
      for (const pair of t.slice(5).trim().split(/\s+/)) {
        const [k, v] = pair.split('=');
        if (k && v) status.clock[k] = parseInt(v, 10);
      }
    }
  }
  return status;
}

// ---------------------------------------------------------------------------
// Load AAPL intraday CSV (node-a)
// ---------------------------------------------------------------------------
// Format: Dates,Open,Close,High,Low,Volume,Number Ticks
function loadIntraBars(filePath) {
  if (!fs.existsSync(filePath)) {
    console.warn(`Data file not found: ${filePath}`);
    return [];
  }
  const raw = fs.readFileSync(filePath, 'utf-8');
  const lines = raw.split('\n').slice(1); // skip header
  const bars = [];
  for (const line of lines) {
    const cols = line.split(',');
    if (cols.length < 7) continue;
    const dateStr = cols[0].trim();
    const open = parseFloat(cols[1]);
    const close = parseFloat(cols[2]);
    const high = parseFloat(cols[3]);
    const low = parseFloat(cols[4]);
    const volume = parseInt(cols[5], 10);
    const ticks = parseInt(cols[6], 10);
    if (isNaN(open) || isNaN(close)) continue;
    bars.push({ dateStr, open, close, high, low, volume, ticks });
  }
  return bars;
}

// ---------------------------------------------------------------------------
// Load bigtech daily CSV (node-b through node-e)
// ---------------------------------------------------------------------------
// Format: tic,datadate,conm,cshtrd,prccd,prchd,prcld,prcod,gvkey
function loadBigtechBars(filePath, ticker) {
  if (!fs.existsSync(filePath)) {
    console.warn(`Data file not found: ${filePath}`);
    return [];
  }
  const raw = fs.readFileSync(filePath, 'utf-8');
  const lines = raw.split('\n').slice(1);
  const bars = [];
  for (const line of lines) {
    const cols = line.split(',');
    if (cols.length < 8) continue;
    if (cols[0].trim() !== ticker) continue;
    const dateStr = cols[1].trim();
    const open = parseFloat(cols[7]);   // prcod
    const close = parseFloat(cols[4]);  // prccd
    const high = parseFloat(cols[5]);   // prchd
    const low = parseFloat(cols[6]);    // prcld
    const volume = Math.round(parseFloat(cols[3])); // cshtrd
    if (isNaN(open) || isNaN(close)) continue;
    bars.push({ dateStr, open, close, high, low, volume, ticks: 0 });
  }
  return bars;
}

// ---------------------------------------------------------------------------
// Load all ticker data
// ---------------------------------------------------------------------------
const tickerBars = {};

// node-a: AAPL intraday
const aaplIntraPath = path.join(DATA_DIR, 'aaplintra.csv');
tickerBars['AAPL'] = loadIntraBars(aaplIntraPath);
console.log(`AAPL (intraday): ${tickerBars['AAPL'].length} bars`);

// node-b..e: daily from bigtech.csv
const bigtechPath = path.join(DATA_DIR, 'bigtech.csv');
for (const [nodeId, ticker] of Object.entries(NODE_TICKERS)) {
  if (ticker === 'AAPL') continue; // already loaded intraday
  tickerBars[ticker] = loadBigtechBars(bigtechPath, ticker);
  console.log(`${ticker} (daily): ${tickerBars[ticker].length} bars`);
}

// The longest dataset determines the replay length
const maxBars = Math.max(...Object.values(tickerBars).map((b) => b.length));

// ---------------------------------------------------------------------------
// Track which C++ nodes are reachable
// ---------------------------------------------------------------------------
const nodeAlive = {};

function anyNodeAlive() {
  return Object.values(nodeAlive).some((v) => v === true);
}

// ---------------------------------------------------------------------------
// Market replay engine
// ---------------------------------------------------------------------------
const replay = {
  running: false,
  speed: 1,
  intervalMs: 400,
  cursor: 0,
  timer: null,
  totalBars: maxBars,
};

function round2(n) {
  return Math.round(n * 100) / 100;
}

function buildSyntheticBook(bar) {
  const bids = [];
  const asks = [];
  const mid = (bar.close + bar.open) / 2;
  const range = bar.high - bar.low;
  const tickSize = Math.max(round2(range * 0.01), 0.01);
  const spread = Math.max(round2(range * 0.02), 0.01);
  const bestBid = round2(mid - spread / 2);
  const bestAsk = round2(mid + spread / 2);

  const sideVol = Math.max(Math.floor(bar.volume / 2000), 10);
  const levels = 10;

  for (let i = 0; i < levels; i++) {
    const step = tickSize * (0.8 + Math.random() * 0.4);
    const bidPrice = round2(bestBid - i * step);
    const bidQty = Math.max(Math.floor(sideVol * (1 - i * 0.07) * (0.6 + Math.random() * 0.8)), 1);
    bids.push({ price: bidPrice, quantity: bidQty });

    const askPrice = round2(bestAsk + i * step);
    const askQty = Math.max(Math.floor(sideVol * (1 - i * 0.07) * (0.6 + Math.random() * 0.8)), 1);
    asks.push({ price: askPrice, quantity: askQty });
  }

  return { bids, asks };
}

function replayTick() {
  if (!replay.running || replay.cursor >= maxBars) {
    stopReplay();
    return;
  }

  const barsThisTick = Math.min(replay.speed, maxBars - replay.cursor);

  for (let b = 0; b < barsThisTick; b++) {
    const idx = replay.cursor + b;

    // For each node, get its ticker's bar at this cursor position
    for (const node of NODES) {
      const ticker = NODE_TICKERS[node.id] || 'AAPL';
      const nodeBars = tickerBars[ticker] || [];
      // Wrap around if this ticker has fewer bars
      const bar = nodeBars.length > 0 ? nodeBars[idx % nodeBars.length] : null;
      if (!bar) continue;

      const book = buildSyntheticBook(bar);
      broadcast({ type: 'book_update', node: node.id, data: book });

      broadcast({
        type: 'status_update',
        node: node.id,
        data: {
          node_id: node.id,
          orders: 10 + Math.floor(Math.random() * 5),
          op_log: idx * 20,
          clock: Object.fromEntries(NODES.map((n, j) => [n.id, idx * 4 + j])),
        },
      });

      // Send per-node bar for the price chart
      broadcast({
        type: 'bar_update',
        node: node.id,
        ticker,
        data: {
          date: bar.dateStr,
          open: bar.open,
          close: bar.close,
          high: bar.high,
          low: bar.low,
          volume: bar.volume,
          ticks: bar.ticks,
        },
        cursor: idx,
        total: replay.totalBars,
      });
    }

    // If C++ nodes are alive, send AAPL orders to node-a
    if (anyNodeAlive()) {
      const aaplBars = tickerBars['AAPL'] || [];
      const aaplBar = aaplBars.length > 0 ? aaplBars[idx % aaplBars.length] : null;
      if (aaplBar) {
        const nodeA = NODES.find((n) => n.id === 'node-a');
        if (nodeA) {
          const mid = (aaplBar.close + aaplBar.open) / 2;
          const range = aaplBar.high - aaplBar.low;
          const spread = Math.max(range * 0.02, 0.01);
          const bestBid = round2(mid - spread / 2);
          const bestAsk = round2(mid + spread / 2);
          const sideVol = Math.max(Math.floor(aaplBar.volume / 2000), 10);
          const levels = 8;
          for (let i = 0; i < levels; i++) {
            const bidPrice = round2(bestBid - i * round2(spread * (0.5 + Math.random() * 0.5)));
            const bidQty = Math.max(Math.floor(sideVol * (1 - i / levels) * (0.5 + Math.random())), 1);
            sendTcp(nodeA.host, nodeA.port, `ADD BUY ${bidPrice.toFixed(2)} ${bidQty}`).catch(() => {});
            const askPrice = round2(bestAsk + i * round2(spread * (0.5 + Math.random() * 0.5)));
            const askQty = Math.max(Math.floor(sideVol * (1 - i / levels) * (0.5 + Math.random())), 1);
            sendTcp(nodeA.host, nodeA.port, `ADD SELL ${askPrice.toFixed(2)} ${askQty}`).catch(() => {});
          }
        }
      }
    }
  }

  replay.cursor += barsThisTick;

  // Send replay status with the AAPL bar for the topbar price display
  const aaplBars = tickerBars['AAPL'] || [];
  const lastAaplBar = aaplBars.length > 0 ? aaplBars[(replay.cursor - 1) % aaplBars.length] : null;

  broadcast({
    type: 'replay_status',
    running: replay.running,
    cursor: replay.cursor,
    total: replay.totalBars,
    speed: replay.speed,
    bar: lastAaplBar
      ? { date: lastAaplBar.dateStr, close: lastAaplBar.close }
      : null,
  });
}

function startReplay() {
  if (replay.running) return;
  if (maxBars === 0) {
    console.warn('No bars loaded, cannot start replay');
    broadcast({ type: 'replay_error', error: 'No data loaded' });
    return;
  }
  if (replay.cursor >= maxBars) replay.cursor = 0;
  replay.running = true;
  replay.timer = setInterval(replayTick, replay.intervalMs);
  console.log(`Replay started at cursor=${replay.cursor} speed=${replay.speed}x`);
  broadcast({
    type: 'replay_status',
    running: true,
    cursor: replay.cursor,
    total: replay.totalBars,
    speed: replay.speed,
  });
}

function stopReplay() {
  replay.running = false;
  if (replay.timer) {
    clearInterval(replay.timer);
    replay.timer = null;
  }
  console.log(`Replay stopped at cursor=${replay.cursor}`);
  broadcast({
    type: 'replay_status',
    running: false,
    cursor: replay.cursor,
    total: replay.totalBars,
    speed: replay.speed,
  });
}

function seekReplay(cursor) {
  replay.cursor = Math.max(0, Math.min(cursor, maxBars - 1));
  broadcast({
    type: 'replay_status',
    running: replay.running,
    cursor: replay.cursor,
    total: replay.totalBars,
    speed: replay.speed,
  });
}

function setReplaySpeed(speed) {
  replay.speed = Math.max(1, Math.min(speed, 100));
  if (replay.running) {
    clearInterval(replay.timer);
    replay.timer = setInterval(replayTick, replay.intervalMs);
  }
  broadcast({
    type: 'replay_status',
    running: replay.running,
    cursor: replay.cursor,
    total: replay.totalBars,
    speed: replay.speed,
  });
}

// ---------------------------------------------------------------------------
// WebSocket server
// ---------------------------------------------------------------------------
const wss = new WebSocketServer({ port: WS_PORT });
const clients = new Set();

function broadcast(msg) {
  const payload = JSON.stringify(msg);
  for (const ws of clients) {
    if (ws.readyState === 1) ws.send(payload);
  }
}

wss.on('connection', (ws) => {
  clients.add(ws);
  console.log(`Client connected (${clients.size} total)`);

  // Send ticker mapping and replay state on connect
  ws.send(JSON.stringify({ type: 'ticker_map', data: NODE_TICKERS }));
  ws.send(
    JSON.stringify({
      type: 'replay_status',
      running: replay.running,
      cursor: replay.cursor,
      total: replay.totalBars,
      speed: replay.speed,
    }),
  );

  ws.on('close', () => {
    clients.delete(ws);
    console.log(`Client disconnected (${clients.size} total)`);
  });

  ws.on('message', async (raw) => {
    try {
      const msg = JSON.parse(raw);

      if (msg.type === 'command') {
        const node = NODES.find((n) => n.id === msg.node);
        if (!node) {
          ws.send(
            JSON.stringify({
              type: 'command_response',
              node: msg.node,
              data: 'ERROR unknown_node',
            }),
          );
          return;
        }
        try {
          const response = await sendTcp(node.host, node.port, msg.command);
          broadcast({
            type: 'command_response',
            node: msg.node,
            command: msg.command,
            data: response,
            ts: Date.now(),
          });
        } catch (err) {
          broadcast({ type: 'node_error', node: msg.node, error: err.message });
        }
      } else if (msg.type === 'replay_start') {
        startReplay();
      } else if (msg.type === 'replay_stop') {
        stopReplay();
      } else if (msg.type === 'replay_seek') {
        seekReplay(msg.cursor || 0);
      } else if (msg.type === 'replay_speed') {
        setReplaySpeed(msg.speed || 1);
      }
    } catch (_) {
      /* ignore malformed messages */
    }
  });
});

// ---------------------------------------------------------------------------
// Polling loop — fetch BOOK + STATUS from every node (only when not replaying)
// ---------------------------------------------------------------------------
async function pollNode(node) {
  try {
    const [bookRaw, statusRaw] = await Promise.all([
      sendTcp(node.host, node.port, 'BOOK 15'),
      sendTcp(node.host, node.port, 'STATUS'),
    ]);

    nodeAlive[node.id] = true;

    if (!replay.running) {
      if (bookRaw) {
        broadcast({ type: 'book_update', node: node.id, data: parseBook(bookRaw) });
      }
      if (statusRaw) {
        broadcast({ type: 'status_update', node: node.id, data: parseStatus(statusRaw) });
      }
    }
  } catch (_) {
    if (nodeAlive[node.id] !== false) {
      nodeAlive[node.id] = false;
      if (!replay.running) {
        broadcast({ type: 'node_error', node: node.id, error: 'Connection refused' });
      }
    }
  }
}

function pollAll() {
  for (const node of NODES) pollNode(node);
}

setInterval(pollAll, POLL_INTERVAL);

// ---------------------------------------------------------------------------
console.log(`Convergence bridge`);
console.log(`  WebSocket : ws://localhost:${WS_PORT}`);
console.log(`  Polling   : ${POLL_INTERVAL}ms`);
console.log(`  Nodes     :`);
for (const node of NODES) {
  const ticker = NODE_TICKERS[node.id] || '???';
  const count = (tickerBars[ticker] || []).length;
  console.log(`    ${node.id} -> ${ticker} (${count} bars) @ ${node.host}:${node.port}`);
}
