import React, { useState, useMemo } from 'react';
import { useSocket } from './hooks/useSocket';
import OrderBook from './components/OrderBook';
import OrderEntry from './components/OrderEntry';
import NodeStatus from './components/NodeStatus';
import ActivityLog from './components/ActivityLog';
import DepthChart from './components/DepthChart';
import ReplayControls from './components/ReplayControls';
import PriceChart from './components/PriceChart';

export default function App() {
  const {
    connected,
    nodes,
    activity,
    sendCommand,
    NODES,
    replay,
    tickerMap,
    priceHistories,
    replayStart,
    replayStop,
    replaySeek,
    replaySetSpeed,
  } = useSocket();

  const [selectedNode, setSelectedNode] = useState('node-a');

  const focusedBook = useMemo(
    () => nodes[selectedNode]?.book || { bids: [], asks: [] },
    [nodes, selectedNode],
  );

  const selectedTicker = tickerMap[selectedNode] || selectedNode;
  const selectedPriceHistory = priceHistories[selectedNode] || [];

  return (
    <div className="app">
      {/* ---- Top bar ---- */}
      <header className="topbar">
        <div className="topbar-left">
          <span className="logo">Convergence</span>
          <span className="subtitle">Distributed Order Book</span>
        </div>
        <div className="topbar-center">
          <span className="ticker-badge">{selectedTicker}</span>
          {replay.bar && selectedNode === 'node-a' && (
            <div className="topbar-price-row">
              <span className="topbar-price">${replay.bar.close?.toFixed(2)}</span>
              <span className="topbar-date">{replay.bar.date}</span>
            </div>
          )}
        </div>
        <div className="topbar-right">
          <span className={`ws-badge ${connected ? 'on' : 'off'}`}>
            {connected ? 'LIVE' : 'DISCONNECTED'}
          </span>
        </div>
      </header>

      {/* ---- Replay controls ---- */}
      <ReplayControls
        replay={replay}
        onStart={replayStart}
        onStop={replayStop}
        onSeek={replaySeek}
        onSetSpeed={replaySetSpeed}
      />

      {/* ---- Main grid ---- */}
      <div className="dashboard">
        {/* Left column — focused book + depth */}
        <section className="books-column">
          <div className="books-tabs">
            {NODES.map((id) => (
              <button
                key={id}
                className={`book-tab ${selectedNode === id ? 'active' : ''} ${nodes[id]?.alive ? '' : 'dead'}`}
                onClick={() => setSelectedNode(id)}
              >
                <span className={`status-dot ${nodes[id]?.alive ? 'green' : 'red'}`} />
                {tickerMap[id] || id}
              </button>
            ))}
          </div>

          <OrderBook book={focusedBook} nodeId={selectedTicker} />
          <DepthChart book={focusedBook} />
        </section>

        {/* Center column — price chart + multi-node books */}
        <section className="center-column">
          <PriceChart priceHistory={selectedPriceHistory} ticker={selectedTicker} />
          <div className="multi-books">
            {NODES.map((id) => (
              <div key={id} className="mini-book-wrapper">
                <OrderBook book={nodes[id].book} nodeId={tickerMap[id] || id} />
              </div>
            ))}
          </div>
        </section>

        {/* Right column — order entry + status + log */}
        <section className="sidebar">
          <OrderEntry sendCommand={sendCommand} nodes={NODES} tickerMap={tickerMap} />
          <NodeStatus nodes={nodes} tickerMap={tickerMap} />
          <ActivityLog activity={activity} />
        </section>
      </div>
    </div>
  );
}
