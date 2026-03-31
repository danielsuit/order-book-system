import React, { useMemo } from 'react';

function ClockViz({ clock }) {
  const entries = Object.entries(clock);
  if (!entries.length) return <span className="dim">empty</span>;
  return (
    <div className="clock-entries">
      {entries.map(([k, v]) => (
        <span key={k} className="clock-entry">
          <span className="clock-node">{k}</span>
          <span className="clock-val">{v}</span>
        </span>
      ))}
    </div>
  );
}

export default function NodeStatus({ nodes, tickerMap = {} }) {
  const convergenceStatus = useMemo(() => {
    const aliveNodes = Object.entries(nodes).filter(([, n]) => n.alive);
    if (aliveNodes.length < 2) return { converged: false, label: 'Insufficient nodes' };

    const serialize = (book) =>
      JSON.stringify([...book.bids, ...book.asks]);
    const states = new Set(aliveNodes.map(([, n]) => serialize(n.book)));
    return {
      converged: states.size === 1,
      label: states.size === 1 ? 'Converged' : `${states.size} distinct states`,
    };
  }, [nodes]);

  return (
    <div className="node-status-panel">
      <div className="panel-header">
        <span className="panel-title">Cluster Status</span>
        <span className={`convergence-badge ${convergenceStatus.converged ? 'converged' : 'diverged'}`}>
          {convergenceStatus.label}
        </span>
      </div>

      <div className="node-grid">
        {Object.entries(nodes).map(([id, n]) => (
          <div key={id} className={`node-card ${n.alive ? 'alive' : 'dead'}`}>
            <div className="node-card-header">
              <span className={`status-dot ${n.alive ? 'green' : 'red'}`} />
              <span className="node-name">{tickerMap[id] || id}</span>
            </div>
            <div className="node-stats">
              <div className="stat">
                <span className="stat-label">Orders</span>
                <span className="stat-value">{n.status.orders}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Op Log</span>
                <span className="stat-value">{n.status.op_log}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Bids</span>
                <span className="stat-value">{n.book.bids.length}</span>
              </div>
              <div className="stat">
                <span className="stat-label">Asks</span>
                <span className="stat-value">{n.book.asks.length}</span>
              </div>
            </div>
            <div className="node-clock">
              <span className="stat-label">Vector Clock</span>
              <ClockViz clock={n.status.clock} />
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
