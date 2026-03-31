import { useEffect, useRef, useCallback, useState } from 'react';

const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:3001';
const RECONNECT_MS = 2000;

const NODES = ['node-a', 'node-b', 'node-c', 'node-d', 'node-e'];

function emptyNodeState() {
  return {
    book: { bids: [], asks: [] },
    status: { node_id: '', orders: 0, op_log: 0, clock: {} },
    alive: false,
    lastUpdate: null,
  };
}

export function useSocket() {
  const wsRef = useRef(null);
  const [connected, setConnected] = useState(false);
  const [nodes, setNodes] = useState(() => {
    const m = {};
    for (const id of NODES) m[id] = emptyNodeState();
    return m;
  });
  const [activity, setActivity] = useState([]);
  const [replay, setReplay] = useState({
    running: false,
    cursor: 0,
    total: 0,
    speed: 1,
    bar: null,
  });
  const [tickerMap, setTickerMap] = useState({
    'node-a': 'AAPL',
    'node-b': 'MSFT',
    'node-c': 'NVDA',
    'node-d': 'AMZN',
    'node-e': 'GOOG',
  });
  // Per-node price history for charts
  const [priceHistories, setPriceHistories] = useState(() => {
    const m = {};
    for (const id of NODES) m[id] = [];
    return m;
  });
  const listenersRef = useRef(new Set());

  const addActivity = useCallback((entry) => {
    setActivity((prev) => [entry, ...prev].slice(0, 200));
  }, []);

  // --- connect --------------------------------------------------------
  useEffect(() => {
    let dead = false;
    let timer = null;

    function connect() {
      if (dead) return;
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setConnected(true);
        addActivity({ type: 'system', text: 'Connected to bridge', ts: Date.now() });
      };

      ws.onclose = () => {
        setConnected(false);
        if (!dead) timer = setTimeout(connect, RECONNECT_MS);
      };

      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data);
          handleMessage(msg);
        } catch (_) { /* ignore */ }
      };
    }

    function handleMessage(msg) {
      if (msg.type === 'book_update') {
        setNodes((prev) => ({
          ...prev,
          [msg.node]: {
            ...prev[msg.node],
            book: msg.data,
            alive: true,
            lastUpdate: Date.now(),
          },
        }));
      } else if (msg.type === 'status_update') {
        setNodes((prev) => ({
          ...prev,
          [msg.node]: {
            ...prev[msg.node],
            status: msg.data,
            alive: true,
            lastUpdate: Date.now(),
          },
        }));
      } else if (msg.type === 'node_error') {
        setNodes((prev) => ({
          ...prev,
          [msg.node]: { ...prev[msg.node], alive: false },
        }));
      } else if (msg.type === 'command_response') {
        addActivity({
          type: 'command',
          node: msg.node,
          command: msg.command,
          data: msg.data,
          ts: msg.ts || Date.now(),
        });
        for (const cb of listenersRef.current) cb(msg);
      } else if (msg.type === 'replay_status') {
        setReplay({
          running: msg.running,
          cursor: msg.cursor,
          total: msg.total,
          speed: msg.speed,
          bar: msg.bar || null,
        });
      } else if (msg.type === 'replay_error') {
        addActivity({ type: 'system', text: `Replay error: ${msg.error}`, ts: Date.now() });
      } else if (msg.type === 'ticker_map') {
        setTickerMap(msg.data);
      } else if (msg.type === 'bar_update') {
        const nodeId = msg.node || 'node-a';
        setPriceHistories((prev) => {
          const arr = prev[nodeId] || [];
          const next = [...arr, msg.data];
          return {
            ...prev,
            [nodeId]: next.length > 300 ? next.slice(next.length - 300) : next,
          };
        });
      }
    }

    connect();
    return () => {
      dead = true;
      clearTimeout(timer);
      if (wsRef.current) wsRef.current.close();
    };
  }, [addActivity]);

  // --- send command ---------------------------------------------------
  const sendCommand = useCallback((nodeId, command) => {
    if (wsRef.current && wsRef.current.readyState === 1) {
      wsRef.current.send(JSON.stringify({ type: 'command', node: nodeId, command }));
      addActivity({ type: 'sent', node: nodeId, command, ts: Date.now() });
    }
  }, [addActivity]);

  const onCommandResponse = useCallback((cb) => {
    listenersRef.current.add(cb);
    return () => listenersRef.current.delete(cb);
  }, []);

  // --- replay controls ------------------------------------------------
  const wsSend = useCallback((msg) => {
    if (wsRef.current?.readyState === 1) {
      wsRef.current.send(JSON.stringify(msg));
      return true;
    }
    addActivity({ type: 'system', text: 'Bridge not connected — start it with: npm run bridge', ts: Date.now() });
    return false;
  }, [addActivity]);

  const replayStart = useCallback(() => {
    wsSend({ type: 'replay_start' });
  }, [wsSend]);

  const replayStop = useCallback(() => {
    wsSend({ type: 'replay_stop' });
  }, [wsSend]);

  const replaySeek = useCallback((cursor) => {
    wsSend({ type: 'replay_seek', cursor });
  }, [wsSend]);

  const replaySetSpeed = useCallback((speed) => {
    wsSend({ type: 'replay_speed', speed });
  }, [wsSend]);

  return {
    connected,
    nodes,
    activity,
    sendCommand,
    onCommandResponse,
    NODES,
    replay,
    tickerMap,
    priceHistories,
    replayStart,
    replayStop,
    replaySeek,
    replaySetSpeed,
  };
}
