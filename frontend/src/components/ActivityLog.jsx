import React from 'react';

function timeStr(ts) {
  const d = new Date(ts);
  return d.toLocaleTimeString('en-US', { hour12: false, fractionalSecondDigits: 3 });
}

function Entry({ item }) {
  if (item.type === 'system') {
    return (
      <div className="log-entry system">
        <span className="log-time">{timeStr(item.ts)}</span>
        <span className="log-text">{item.text}</span>
      </div>
    );
  }
  if (item.type === 'sent') {
    return (
      <div className="log-entry sent">
        <span className="log-time">{timeStr(item.ts)}</span>
        <span className="log-arrow">&rarr;</span>
        <span className="log-node">{item.node}</span>
        <span className="log-cmd">{item.command}</span>
      </div>
    );
  }
  if (item.type === 'command') {
    const isOk = item.data && item.data.startsWith('OK');
    const isFilled = item.data && item.data.startsWith('FILLED');
    const isError = item.data && item.data.startsWith('ERROR');
    return (
      <div className={`log-entry response ${isError ? 'error' : isOk || isFilled ? 'ok' : ''}`}>
        <span className="log-time">{timeStr(item.ts)}</span>
        <span className="log-arrow">&larr;</span>
        <span className="log-node">{item.node}</span>
        <span className="log-data">{item.data}</span>
      </div>
    );
  }
  return null;
}

export default function ActivityLog({ activity }) {
  return (
    <div className="activity-log">
      <div className="panel-header">
        <span className="panel-title">Activity Log</span>
        <span className="dim">{activity.length} events</span>
      </div>
      <div className="log-list">
        {activity.length === 0 && (
          <div className="log-empty">No activity yet. Place an order to get started.</div>
        )}
        {activity.map((item, i) => (
          <Entry key={i} item={item} />
        ))}
      </div>
    </div>
  );
}
