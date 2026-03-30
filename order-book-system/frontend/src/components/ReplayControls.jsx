import React from 'react';

const SPEED_OPTIONS = [1, 2, 5, 10, 25, 50];

export default function ReplayControls({
  replay,
  onStart,
  onStop,
  onSeek,
  onSetSpeed,
}) {
  const pct = replay.total > 0 ? (replay.cursor / replay.total) * 100 : 0;

  return (
    <div className="replay-controls">
      <div className="replay-header">
        <span className="replay-title">Market Replay</span>
        <span className={`replay-badge ${replay.running ? 'running' : 'paused'}`}>
          {replay.running ? 'PLAYING' : 'PAUSED'}
        </span>
      </div>

      {replay.bar && (
        <div className="replay-bar-info">
          <span className="replay-date">{replay.bar.date}</span>
          <span className="replay-price">${replay.bar.close?.toFixed(2)}</span>
        </div>
      )}

      <div className="replay-progress-row">
        <span className="replay-cursor">{replay.cursor.toLocaleString()}</span>
        <div className="replay-progress-track" onClick={(e) => {
          const rect = e.currentTarget.getBoundingClientRect();
          const ratio = (e.clientX - rect.left) / rect.width;
          onSeek(Math.floor(ratio * replay.total));
        }}>
          <div className="replay-progress-fill" style={{ width: `${pct}%` }} />
        </div>
        <span className="replay-total">{replay.total.toLocaleString()}</span>
      </div>

      <div className="replay-buttons">
        <button
          className="replay-btn"
          onClick={replay.running ? onStop : onStart}
        >
          {replay.running ? '⏸ Pause' : '▶ Play'}
        </button>

        <button className="replay-btn" onClick={() => onSeek(0)}>
          ⏮ Reset
        </button>

        <div className="speed-group">
          <span className="speed-label">Speed</span>
          {SPEED_OPTIONS.map((s) => (
            <button
              key={s}
              className={`speed-btn ${replay.speed === s ? 'active' : ''}`}
              onClick={() => onSetSpeed(s)}
            >
              {s}x
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
